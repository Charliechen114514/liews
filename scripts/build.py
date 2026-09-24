#!/usr/bin/env python3
"""build.py — atomic build steps; orchestration lives in CMakeLists.txt.

  build  = gn gen + ninja (ui.views tree; own code stays out of GN)
  bridge = export GN compile flags / link closure for the root CMake project
  compdb = dump tree compile_commands and merge with CMake-side entries
  deploy = copy the minimal runtime set

Usage:
  python scripts/build.py [build|bridge|compdb|deploy]
                          [--target <tree>] [--out <dir>] [--deploy <dir>]
Defaults: --target = repo's ui.views tree, --out = <repo>/build/Release,
--deploy = <repo>/build/deploy.

Toolchain (independent of any chromium checkout): in-tree gn
(buildtools/win/gn.exe), system python, local VS, clang-cl + rustc + ninja +
bindgen on PATH (probed, smoke-tested). Escape hatches: CLANG_BASE,
RUST_SYSROOT, RUST_BINDGEN, RUSTC_VERSION. Probe failure is fatal.
"""
import argparse
import filecmp
import json
import os
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

IS_WIN = os.name == "nt"
DIE_PREFIX = "[build] error: "


def die(msg):
    print(DIE_PREFIX + msg, file=sys.stderr)
    sys.exit(1)


def info(msg):
    print("[build] " + msg, flush=True)


def which(tool):
    return shutil.which(tool)


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", **kw)


def run_logged(cmd, log_path, **kw):
    """Run a child and stream+tee its combined output."""
    with open(log_path, "ab") as log:
        log.write(("\n===== %s =====\n" % " ".join(cmd[:4])).encode("utf-8"))
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True,
                         encoding="utf-8", errors="replace", **kw)
    with open(log_path, "ab") as log:
        for line in p.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            log.write(line.encode("utf-8"))
    return p.wait()


GN_EXAMPLES = ("views_smoke", "views_media_smoke")


def sync_local_gn_examples(repo, target):
    """Materialize root-canonical GN examples in an ignored GN overlay."""
    local_tree = os.path.realpath(os.path.join(repo, "ui.views"))
    if os.path.normcase(os.path.realpath(target)) != os.path.normcase(local_tree):
        return

    def files(root):
        result = {}
        if not os.path.isdir(root):
            return result
        for base, _dirs, names in os.walk(root):
            for name in names:
                path = os.path.join(base, name)
                result[os.path.relpath(path, root)] = path
        return result

    synced = []
    for example in GN_EXAMPLES:
        src = os.path.join(repo, "examples", example)
        dst = os.path.abspath(os.path.join(
            target, ".liew", "examples", example))
        if not os.path.isdir(src):
            die(f"canonical GN example missing: {src}")

        # These directories are generated and may be replaced wholesale, but
        # refuse to follow a junction/symlink outside the local vendor tree.
        dst_real = os.path.realpath(dst)
        try:
            inside = os.path.commonpath([local_tree, dst_real]) == local_tree
        except ValueError:
            inside = False
        if not inside:
            die(f"refusing to replace GN example outside ui.views: {dst_real}")

        src_files = files(src)
        dst_files = files(dst)
        same = src_files.keys() == dst_files.keys() and all(
            filecmp.cmp(src_files[rel], dst_files[rel], shallow=False)
            for rel in src_files)
        if same:
            continue
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        elif os.path.lexists(dst):
            os.remove(dst)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copytree(src, dst)
        synced.append(example)

    if synced:
        info("GN example overlay synced: " + ", ".join(synced))


def remap_compdb(entries, target, repo):
    """Rewrite tree-side GN example paths back to the repo originals."""
    def to_posix(p):
        return os.path.normpath(p).replace(os.sep, "/")

    mapping = [(to_posix(os.path.join(target, ".liew", "examples")) + "/",
                to_posix(os.path.join(repo, "examples")) + "/")]

    n = 0
    for e in entries:
        f = e.get("file", "")
        directory = e.get("directory", "")
        fa = to_posix(f if os.path.isabs(f) else os.path.join(directory, f))
        for old, new in mapping:
            if not fa.lower().startswith(old.lower()):
                continue
            nf = new + fa[len(old):]
            cmd = e.get("command", "")
            for tok in (f, fa, to_posix(os.path.relpath(fa, directory))):
                if tok and tok in cmd:
                    cmd = cmd.replace(tok, nf)
            e["command"] = cmd
            e["file"] = nf
            n += 1
            break
    return n


def write_bridge(repo, target, out_dir, gn, ninja):
    """Export GN's compile/link recipe as CMake-includeable lists and an rsp."""
    import io
    import re
    import shlex

    def cmake_quote(s):
        return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

    def abs_posix(p):
        p = p.replace('/', os.sep)
        a = p if os.path.isabs(p) else os.path.normpath(os.path.join(out_dir, p))
        return a.replace(os.sep, '/')

    def rebase_opt(t):
        # Rebase ../-relative flag values onto out_dir; they break under CMake's cwd.
        m = re.match(r'^(--[^\s=]+|--?[^\s=]+)=(.+)$', t)
        if not m or os.path.isabs(m.group(2)):
            return t
        val = m.group(2)
        if val.startswith('../'):
            return f"{m.group(1)}={abs_posix(val)}"
        if val.startswith('./'):
            ap = abs_posix(val)
            return f"{m.group(1)}={ap}" if os.path.exists(ap) else t
        return t

    # ---- compile recipe: the compdb reference entry's command line ----
    r = subprocess.run([ninja, "-C", out_dir, "-t", "compdb", "cxx", "cc"],
                       capture_output=True)
    if r.returncode != 0:
        die("bridge: ninja -t compdb failed")
    entries = json.loads(r.stdout.decode("utf-8", "replace"))
    ref = next((e for e in entries if e.get("file", "").replace("\\", "/").endswith(
        "examples/views_smoke/liew_link_recipe.cc")), None)
    if ref is None:
        die("bridge: no liew_link_recipe reference entry in compdb")

    # Quoted-value macros get ripped out whole; CMake re-defines them (see
    # liew/CMakeLists.txt) because nested quotes never survive the Windows
    # command line.
    raw_cmd = re.sub(r'"-D\w+=\\"[^"]*\\""', " ", ref["command"])
    toks = [t.strip('"') for t in shlex.split(raw_cmd, posix=False)]
    incs, opts, sys_incs = [], [], []
    skip_prefix = ("/c", "/Fo", "/Fd", "/showIncludes")
    i = 1
    while i < len(toks):
        t = toks[i]
        i += 1
        if (t in ("/nologo", "/TP") or t.endswith((".cc", ".c", ".cpp"))
                or any(t.startswith(p) for p in skip_prefix)):
            continue
        if t == "-mllvm" and i < len(toks):
            opts.append(f"-mllvm={toks[i]}")  # joined form survives CMake lists
            i += 1
            continue
        if '"' in t:
            continue
        if t.startswith(("-imsvc", "/imsvc")):
            # System rank (INCLUDE-env-like): must stay BELOW the -I rank or
            # the MSVC/SDK STL shadows the tree's libc++ (__Cr ABI namespace).
            sys_incs.append(abs_posix(t[6:]))
        elif t.startswith(("-I", "/I")):
            incs.append(abs_posix(t[2:]))
        else:
            opts.append(rebase_opt(t))

    # ---- link recipe: the link edge's EXPLICIT inputs from build.ninja ----
    # (ninja -t inputs is transitive incl. order-only noise; it pulled both
    # proto variants as raw objs and produced duplicate symbols at link time)
    host_main = ("obj/.liew/examples/views_smoke/liew_link_recipe/"
                 "liew_link_recipe.obj")
    edge_re = re.compile(
        r'^build [^:]*\bliew_link_recipe\.exe\b[^:]*: link\b')
    raw, started = [], False
    # GN writes per-target ninja files; the exe's link edge lives in its own.
    for nf in ("obj/.liew/examples/views_smoke/liew_link_recipe.ninja",
               "build.ninja", "toolchain.ninja"):
        path = os.path.join(out_dir, nf)
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as fh:
            for ln in fh:
                ln = ln.rstrip("\n")
                if not started:
                    if edge_re.match(ln):
                        started = True
                        raw.append(ln)
                    continue
                if ln[:1] in (" ", "\t"):
                    raw.append(ln)
                else:
                    break
        if started:
            break
    if not started:
        die("bridge: link edge for liew_link_recipe.exe not found in ninja files")
    edge = " ".join(x.strip() for x in raw)
    m = re.search(r':\s*link\s+(.*?)\s*\|', edge)
    if not m:
        die("bridge: cannot parse link edge inputs")
    link_inputs = []
    for p in m.group(1).replace("$", " ").split():
        if p.endswith((".obj", ".lib", ".rlib")):  # rlib: rust cxxbridge glue
                                                  # (box alloc/dealloc etc.)
            ap = abs_posix(p.strip('"'))
            if not ap.endswith("/" + host_main):
                link_inputs.append(ap)
    # GN passes rust libs via an edge variable, not explicit inputs; without
    # them the cxxbridge glue symbols (box $alloc/$dealloc etc.) go undefined.
    for x in raw:
        m2 = re.match(r'rlibs\s*=\s*(.*)', x.strip())
        if m2:
            link_inputs += [abs_posix(p) for p in m2.group(1).split()
                            if p.endswith(".rlib")]
    # ldflags carries two things the rsp closure needs: the tree's CRT choice
    # (/DEFAULTLIB:libcpmt.lib — obj directives alone pull only libcmt, and
    # __ExceptionPtr* lives in the static C++ runtime) and the sysroot rust
    # rlibs (core/std/alloc/panic_abort/...) as explicit link inputs.
    defaultlibs = []
    for x in raw:
        m3 = re.match(r'ldflags\s*=\s*(.*)', x.strip())
        if m3:
            toks = [t.replace("$:", ":") for t in m3.group(1).split()]
            defaultlibs = [t for t in toks
                           if t.upper().startswith("/DEFAULTLIB")]
            link_inputs += [abs_posix(t) for t in toks if t.endswith(".rlib")]
    link_inputs = sorted(set(link_inputs))

    env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0")
    out_rel = os.path.relpath(out_dir, target).replace(os.sep, "/")
    r = subprocess.run(
        [gn, "desc", out_rel,
         "//.liew/examples/views_smoke:liew_link_recipe",
         "libs", "--all"], cwd=target, env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace")
    if r.returncode != 0:
        die("bridge: gn desc libs failed: " + r.stderr.strip()[:200])
    syslibs = []
    for line in r.stdout.splitlines():
        lib = line.strip()
        if not lib:
            continue
        if re.match(r'^/[A-Za-z]:[/\\]', lib):  # clang_rt as "/<drive>:/..."
            lib = lib[1:].replace("\\", "/")
        syslibs.append(lib)

    # The tree's link command carries -libpath: dirs (MSVC/SDK CRT import
    # libs); lld-link needs them to satisfy /DEFAULTLIB directives embedded
    # in objs (msvcprt etc.). Without them the rust rlibs' __ExceptionPtr*
    # and friends stay undefined.
    r = subprocess.run([ninja, "-C", out_dir, "-t", "commands",
                        "liew_link_recipe.exe"], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    libpaths = []
    if r.returncode == 0:
        for ln in r.stdout.splitlines():
            if ("lld-link" not in ln or
                    "/OUT:./liew_link_recipe.exe" not in ln):
                continue
            found = re.findall(r'"-libpath:([^"]+)"', ln)
            found += [t[len("-libpath:"):] for t in ln.split()
                      if t.startswith("-libpath:")]
            if found:  # a bare prefix-matching line carries no libpaths
                # GN-relative dirs ("../../../llvm/...") resolve against the
                # tree's out dir; rebase or they dangle under CMake's build/.
                libpaths = [p if os.path.isabs(p) else abs_posix(p)
                            for p in found]
                break
    if not libpaths:
        die(f"bridge: no -libpath in liew_link_recipe link command "
            f"(rc={r.returncode}, stderr={r.stderr[:200]!r})")

    def write_list(fh, name, items):
        fh.write(f"set({name}\n")
        for it in items:
            fh.write("  " + cmake_quote(it) + "\n")
        fh.write(")\n\n")

    def write_if_changed(path, content):
        try:
            with open(path, encoding="utf-8") as fh:
                if fh.read() == content:
                    return
        except FileNotFoundError:
            pass
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(content)

    build_dir = os.path.join(repo, "build")
    os.makedirs(build_dir, exist_ok=True)
    # [cleanbootstrap] GN include dirs under out_dir only materialize when
    # ninja runs its actions, but cmake generate checks them first; pre-create
    # empty dirs so a clean checkout configures, ninja fills real files later.
    out_prefix = out_dir.replace(os.sep, "/") + "/"
    for d in incs + sys_incs:
        if d.startswith(out_prefix):
            os.makedirs(d, exist_ok=True)
    flags = io.StringIO()
    flags.write("# Generated by scripts/build.py bridge - do not edit.\n\n")
    write_list(flags, "LIEW_CXX_INCLUDES", incs)
    write_list(flags, "LIEW_CXX_SYSTEM_INCLUDES", sys_incs)
    write_list(flags, "LIEW_CXX_OPTIONS", opts)
    write_if_changed(os.path.join(build_dir, "liew_flags.cmake"),
                     flags.getvalue())
    rsp_path = os.path.join(build_dir, "liew_link.rsp")
    write_if_changed(rsp_path, "\n".join(link_inputs) + "\n")
    link = io.StringIO()
    link.write("# Generated by scripts/build.py bridge - do not edit.\n")
    link.write("# Closure lives in liew_link.rsp (host main.obj excluded).\n\n")
    link.write(f'set(LIEW_LINK_RSP "{rsp_path.replace(os.sep, "/")}")\n\n')
    write_list(link, "LIEW_LINK_SYSTEM_LIBS", syslibs)
    write_list(link, "LIEW_LINK_LIBPATHS",
               [f"-libpath:{p}" for p in libpaths])
    write_list(link, "LIEW_LINK_DEFAULTLIBS", defaultlibs)
    write_if_changed(os.path.join(build_dir, "liew_link.cmake"),
                     link.getvalue())
    info(f"bridge done: {len(incs)}+{len(sys_incs)} includes, {len(opts)} "
         f"options, {len(link_inputs)} link inputs (rsp), {len(syslibs)} "
         f"system libs, {len(libpaths)} libpaths")


def probe_tool(name, smoke_arg="--version", env_key=None, hint=""):
    """PATH probe + smoke run; returns the absolute exe path."""
    if env_key and os.environ.get(env_key):
        return os.environ[env_key]
    p = which(name)
    if not p:
        die(f"{name} not found on PATH: {hint}")
    r = run([p, smoke_arg])
    if r.returncode != 0:
        die(f"{name} on PATH failed smoke test: {hint}")
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("phase", nargs="?", default="build",
                    choices=["build", "bridge", "compdb", "deploy"])
    ap.add_argument("--target", default=None,
                    help="tree to build (default = repo's ui.views tree)")
    ap.add_argument("--out", default=None,
                    help="build output dir (default <repo>/build/Release)")
    ap.add_argument("--deploy", default=None,
                    help="deploy output dir (default <repo>/build/deploy)")
    args = ap.parse_args()

    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
    default_target = os.path.join(repo, "ui.views")
    target = os.path.abspath(args.target or (
        default_target if os.path.isfile(os.path.join(default_target, ".gn"))
        else repo))
    out_dir = os.path.abspath(
        args.out or os.path.join(repo, "build", "Release"))
    deploy = args.deploy or os.path.join(repo, "build", "deploy")
    log = os.path.join(repo, "build", "build.log")
    os.makedirs(os.path.join(repo, "build"), exist_ok=True)

    # ---- toolchain resolution (env > PATH probe; failure is fatal) ----
    clang_base = os.environ.get("CLANG_BASE")
    if not clang_base:
        cc = which("clang-cl") or which("clang++")
        if not cc:
            die("clang-cl/clang++ not on PATH: install LLVM, or set CLANG_BASE")
        clang_base = os.path.dirname(os.path.dirname(cc))

    rust_sysroot = os.environ.get("RUST_SYSROOT")
    if not rust_sysroot:
        rc = which("rustc")
        if not rc:
            die("rustc (rustup) not on PATH, or set RUST_SYSROOT")
        r = run([rc, "--print", "sysroot"])
        rust_sysroot = r.stdout.strip()
    rustc = os.path.join(rust_sysroot, "bin",
                         "rustc.exe" if IS_WIN else "rustc")
    if not os.path.isfile(rustc):
        die(f"rustc missing: {rustc}")

    rust_bindgen = os.environ.get("RUST_BINDGEN")
    if not rust_bindgen:
        bg = probe_tool(
            "bindgen", hint="cargo install bindgen-cli, or set RUST_BINDGEN "
                            "to its root dir (parent of bin/)")
        rust_bindgen = os.path.dirname(os.path.dirname(os.path.abspath(bg)))

    rustc_version = os.environ.get("RUSTC_VERSION")
    if not rustc_version:
        r = run([rustc, "--version"])
        import re
        m = re.search(r"rustc (\d[\d.]*-nightly) \(([0-9a-f]{8})", r.stdout)
        if not m:
            die(f"cannot parse rustc version (set RUSTC_VERSION): "
                f"{r.stdout.strip()}")
        rustc_version = f"{m.group(1)}-{m.group(2)}"

    # On Windows, asking shutil.which() for bare "ninja" may select
    # depot_tools/ninja.bat. Its Python/caffeinate wrapper can hang while
    # launching a real build even though read-only `-t` commands work. Require
    # the native executable so the build process and its output are direct.
    ninja = probe_tool("ninja.exe" if IS_WIN else "ninja",
                       hint="winget install Ninja.Ninja "
                            "(depot_tools shims don't count)")
    info(f"toolchain: ninja = {ninja} "
         f"({run([ninja, '--version']).stdout.strip()})")
    info(f"toolchain: clang = {clang_base} / rust = {rustc_version} "
         f"@ {rust_sysroot}")

    # ---- clang junction: rc.py reaches clang via a fixed tree-relative path,
    #      ignoring clang_base_path; self-heal like the llvm-build layout ----
    if IS_WIN:
        jb = os.path.join(target, "third_party", "llvm-build",
                          "Release+Asserts", "bin", "clang-cl.exe")
        jdir = os.path.dirname(os.path.dirname(jb))
        if not os.path.isfile(jb):
            os.makedirs(os.path.dirname(jdir), exist_ok=True)
            if os.path.lexists(jdir):  # dangling junction: exists() is False
                os.rmdir(jdir)
            import _winapi
            _winapi.CreateJunction(clang_base, jdir)
            info(f"clang junction created -> {clang_base}")

    if args.phase in ("build", "bridge"):
        if not os.path.isfile(os.path.join(target, ".gn")):
            die(f"target tree missing: {target} (no .gn), or wrong path")
        sync_local_gn_examples(repo, target)
        gn = os.path.join(target, "buildtools", "win", "gn.exe")
        if not os.path.isfile(gn):
            gn = which("gn") or die(
                f"in-tree gn missing: {gn} (tree sync gap), no gn on PATH")

        # args.gn is regenerated every run = repo args.gn + local injection.
        os.makedirs(out_dir, exist_ok=True)
        with open(os.path.join(repo, "args.gn"), encoding="utf-8") as fh:
            body = fh.read()
        body += ("\n# --- local injection (scripts/build.py) ---\n"
                 f'clang_base_path = "{clang_base}"\n'
                 f'rustc_version = "{rustc_version}"\n'
                 f'rust_sysroot_absolute = "{rust_sysroot}"\n'
                 f'rust_bindgen_root = "{rust_bindgen}"\n')

        # Skip gen when the graph cannot have changed: identical args.gn
        # content and older key build files. Force by touching ui.views/BUILD.gn
        # or deleting <out>/args.gn.
        out_args = os.path.join(out_dir, "args.gn")
        out_manifest = os.path.join(out_dir, "build.ninja")
        need_gen = True
        if os.path.isfile(out_args) and os.path.isfile(out_manifest):
            with open(out_args, encoding="utf-8") as fh:
                need_gen = fh.read() != body
            if not need_gen:
                manifest_mt = os.path.getmtime(out_manifest)
                for guard in (os.path.join(target, ".gn"),
                              os.path.join(target, "BUILD.gn"),
                              os.path.join(target, ".liew", "examples")):
                    if os.path.getmtime(guard) > manifest_mt:
                        need_gen = True
                        break
        if need_gen:
            with open(out_args, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(body)
            info("args.gn written (repo/args.gn + local injection)")
            env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0")
            info("build: gn gen")
            # GN accepts out dirs outside the tree and rebases all paths itself.
            out_rel = os.path.relpath(out_dir, target).replace(os.sep, "/")
            r = run_logged([gn, "gen", out_rel], log, cwd=target, env=env)
            if r != 0:
                die(f"gn gen failed (exit={r})")
        else:
            env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0")
            info("build: gn gen skipped (args + graph unchanged)")

        # Bridge files refresh with every gen (extraction is read-only, fast).
        if args.phase in ("bridge", "build"):
            write_bridge(repo, target, out_dir, gn, ninja)

    if args.phase == "build":
        info("build: ninja")
        # The default graph also catalogs views_smoke so its link recipe is
        # available to the CMake bridge. Build only the reusable closure here:
        # the tree example is an explicit diagnostic, not a daily artifact.
        r = run_logged([ninja, "-C", out_dir, "liew_bridge"], log, env=env)
        if r != 0:
            die(f"ninja failed (exit={r})")
        info("build done")

    if args.phase == "compdb":
        # No gen here: compdb only reads build.ninja — the pipeline always
        # runs right after build.py build which just did gen + ninja.
        if not os.path.isfile(os.path.join(out_dir, "build.ninja")):
            die("no build.ninja in the out dir; run build first")
        info("compdb: dumping tree entries (ninja -t compdb cxx cc)")
        r = subprocess.run([ninja, "-C", out_dir, "-t", "compdb",
                            "cxx", "cc"], capture_output=True)
        if r.returncode != 0:
            die("compdb dump failed (exit=%d): %s" % (
                r.returncode, r.stderr.decode("utf-8", "replace")[:300]))
        entries = json.loads(r.stdout.decode("utf-8", "replace"))
        remapped = remap_compdb(entries, target, repo)
        ccdb = os.path.join(repo, "build", "compile_commands.json")
        # Merge with CMake's own entries (CMake rewrites the file on every
        # reconfigure; this phase restores the union, deduped by file).
        base = []
        if os.path.isfile(ccdb):
            try:
                with open(ccdb, encoding="utf-8") as fh:
                    base = [e for e in json.load(fh)
                            if isinstance(e, dict) and e.get("file")]
            except (ValueError, OSError):
                base = []
        have = {e["file"].replace("\\", "/").lower() for e in base}
        merged = base + [e for e in entries
                         if e["file"].replace("\\", "/").lower() not in have]
        with open(ccdb, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(merged, fh)
        info(f"compdb done: {ccdb}")
        info(f"tree {len(entries)} (remapped {remapped}) + cmake {len(base)}"
             f" = {len(merged)} entries (merged compile db)")

    if args.phase == "deploy":
        rdir = out_dir
        liew_dir = os.path.join(repo, "build", "liew")
        liew_dll = os.path.join(liew_dir, "liew.dll" if IS_WIN else "libliew.so")
        smoke = os.path.join(repo, "build", "examples",
                             "liew-smoke.exe" if IS_WIN else "liew-smoke")
        if not os.path.isfile(liew_dll) or not os.path.isfile(smoke):
            die("missing liew runtime/example (run the CMake build first)")
        os.makedirs(deploy, exist_ok=True)
        # Older revisions deployed the tree-owned diagnostic executables.
        # Remove those known stale artifacts so an existing deploy directory
        # converges to the new DLL + consumer runtime set.
        for stem in ("views_smoke", "views_media_smoke"):
            for ext in (".exe", ".pdb", ".lib"):
                stale = os.path.join(deploy, stem + ext)
                if os.path.isfile(stale):
                    os.remove(stale)
        for name in ("ui_test.pak", "ui_resources_100_percent.pak"):
            stale = os.path.join(deploy, name)
            if os.path.isfile(stale):
                os.remove(stale)
        shutil.copy2(liew_dll, deploy)
        shutil.copy2(smoke, deploy)
        files = ["libEGL.dll", "libGLESv2.dll", "d3dcompiler_47.dll",
                 "icudtl.dat", "liew_resources.pak"]
        for f in files:
            src = os.path.join(rdir, f)
            if os.path.isfile(src):
                shutil.copy2(src, deploy)
        info(f"deploy done: {deploy}")
        info("check: run deploy/liew-smoke, open and close the window; it"
             " should exit 0")


if __name__ == "__main__":
    main()
