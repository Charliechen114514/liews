#!/usr/bin/env python3
"""build.py — 构建与部署

只做两件事: build (gn gen + ninja) / deploy (拷最小部署集)。
不做提取 —— 树的同步与裁剪由 update.py + select.py 负责。

用法:
  python tools/build.py [build|deploy|all] [--target <树路径>] [--deploy <目录>]
  --target 缺省 = 本仓库根 (就地构建); --deploy 缺省 = <树>/out/deploy

依赖 (与 chromium 源树无关):
  树内 gn (buildtools/win/gn.exe) / 系统 python / 本机 VS /
  PATH 上的 clang-cl + rustc + ninja + bindgen (探测 + 冒烟验证 + 位置反推)。
  工具链逃生口 (带前缀, 通用名一律不读):
    CLANG_BASE / RUST_SYSROOT / RUST_BINDGEN / RUSTC_VERSION
  探测失败立即报错, 不做静默回退。
"""
import argparse
import os
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

IS_WIN = os.name == "nt"
DIE_PREFIX = "[build] 错误: "


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


def probe_tool(name, smoke_arg="--version", env_key=None, hint=""):
    """PATH 探测 + 实跑冒烟; 返回 exe 绝对路径。"""
    if env_key and os.environ.get(env_key):
        return os.environ[env_key]
    p = which(name)
    if not p:
        die(f"PATH 上找不到 {name}: {hint}")
    r = run([p, smoke_arg])
    if r.returncode != 0:
        die(f"PATH 上的 {name} 不可用 (冒烟失败): {hint}")
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("phase", nargs="?", default="all",
                    choices=["build", "deploy", "all"])
    ap.add_argument("--target", default=None, help="要构建的树 (默认 = 本仓库根)")
    ap.add_argument("--deploy", default=None, help="部署输出目录 (默认 <树>/out/deploy)")
    args = ap.parse_args()

    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
    target = os.path.abspath(args.target or repo)
    deploy = args.deploy or os.path.join(target, "out", "deploy")

    # ---- 工具链解析 (环境变量 > PATH 探测; 失败即死) ----
    clang_base = os.environ.get("CLANG_BASE")
    if not clang_base:
        cc = which("clang-cl") or which("clang++")
        if not cc:
            die("PATH 上找不到 clang-cl/clang++: 安装 LLVM 并加入 PATH, 或设 CLANG_BASE")
        clang_base = os.path.dirname(os.path.dirname(cc))

    rust_sysroot = os.environ.get("RUST_SYSROOT")
    if not rust_sysroot:
        rc = which("rustc")
        if not rc:
            die("PATH 上找不到 rustc (rustup), 或设 RUST_SYSROOT")
        r = run([rc, "--print", "sysroot"])
        rust_sysroot = r.stdout.strip()
    rustc = os.path.join(rust_sysroot, "bin",
                         "rustc.exe" if IS_WIN else "rustc")
    if not os.path.isfile(rustc):
        die(f"rustc 不存在: {rustc}")

    rust_bindgen = os.environ.get("RUST_BINDGEN")
    if not rust_bindgen:
        bg = probe_tool(
            "bindgen", hint="cargo install bindgen-cli (装完即在 rustup 的 PATH 上), "
                            "或设 RUST_BINDGEN 指向其根目录 (bindgen 的 bin/ 上一级)")
        rust_bindgen = os.path.dirname(os.path.dirname(os.path.abspath(bg)))

    rustc_version = os.environ.get("RUSTC_VERSION")
    if not rustc_version:
        r = run([rustc, "--version"])
        import re
        m = re.search(r"rustc (\d[\d.]*-nightly) \(([0-9a-f]{8})", r.stdout)
        if not m:
            die(f"无法解析 rustc 版本 (设 RUSTC_VERSION, 不能含空格): {r.stdout.strip()}")
        rustc_version = f"{m.group(1)}-{m.group(2)}"

    ninja = probe_tool("ninja", hint="winget install Ninja.Ninja "
                                     "(depot_tools 的 shim 不算), 或设 PATH")
    info(f"工具链: clang = {clang_base} / rust = {rustc_version} @ {rust_sysroot}")

    # ---- clang junction: rc.py 按固定相对路径
    # third_party/llvm-build/Release+Asserts/bin/clang-cl 调用 clang (无视
    # clang_base_path)。悬尸自愈: 检测点 = bin 下 clang 可达, 非 junction 存在。
    if IS_WIN:
        jb = os.path.join(target, "third_party", "llvm-build",
                          "Release+Asserts", "bin", "clang-cl.exe")
        jdir = os.path.dirname(os.path.dirname(jb))
        if not os.path.isfile(jb):
            os.makedirs(os.path.dirname(jdir), exist_ok=True)
            if os.path.lexists(jdir):  # lexists: 悬尸 junction exists() 为 False
                os.rmdir(jdir)         # junction/空目录均可 rmdir
            import _winapi
            _winapi.CreateJunction(clang_base, jdir)
            info(f"clang junction 已建 -> {clang_base}")

    if args.phase in ("build", "all"):
        if not os.path.isfile(os.path.join(target, ".gn")):
            die(f"目标树不存在: {target} (缺 .gn), 或路径不对")
        gn = os.path.join(target, "buildtools", "win", "gn.exe")
        if not os.path.isfile(gn):
            gn = which("gn") or die(
                f"树内 gn 不存在: {gn} (树同步缺失), PATH 上也没有 gn")

        # args.gn 每次重生成 = 仓库根 args.gn (产品参数权威源) + 本机注入。
        out_dir = os.path.join(target, "out", "Release")
        os.makedirs(out_dir, exist_ok=True)
        with open(os.path.join(repo, "args.gn"), encoding="utf-8") as fh:
            body = fh.read()
        body += ("\n# --- 本机注入 (tools/build.py; 可用环境变量覆写) ---\n"
                 f'clang_base_path = "{clang_base}"\n'
                 f'rustc_version = "{rustc_version}"\n'
                 f'rust_sysroot_absolute = "{rust_sysroot}"\n'
                 f'rust_bindgen_root = "{rust_bindgen}"\n')
        with open(os.path.join(out_dir, "args.gn"), "w",
                  encoding="utf-8", newline="\n") as fh:
            fh.write(body)
        info("args.gn 已生成 (repo/args.gn + 本机注入)")

        env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0")
        info("build: gn gen")
        r = subprocess.call([gn, "gen", "out/Release"], cwd=target, env=env)
        if r != 0:
            die(f"gn gen 失败 (exit={r})")

        info("build: ninja")
        r = subprocess.call([ninja, "-C", out_dir], env=env)
        if r != 0:
            die(f"ninja 失败 (exit={r})")
        info("build 完成")

    if args.phase in ("deploy", "all"):
        rdir = os.path.join(target, "out", "Release")
        exe = os.path.join(rdir, "views_smoke.exe" if IS_WIN else "views_smoke")
        if not os.path.isfile(exe):
            die(f"缺少 {exe} (先运行 build)")
        os.makedirs(deploy, exist_ok=True)
        files = ["views_smoke.exe", "libEGL.dll", "libGLESv2.dll",
                 "d3dcompiler_47.dll", "icudtl.dat", "ui_test.pak",
                 "ui_resources_100_percent.pak"]
        for f in files:
            src = os.path.join(rdir, f)
            if os.path.isfile(src):
                shutil.copy2(src, deploy)
        info(f"deploy 完成: {deploy}")
        info("判据 1: 运行 views_smoke, 应弹出 'views-standalone smoke' 窗口")
        info(f"判据 2: 运行 {rdir}/views_media_smoke, 应列出音频设备且退出码 0")


if __name__ == "__main__":
    main()
