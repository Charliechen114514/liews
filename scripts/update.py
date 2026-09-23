#!/usr/bin/env python3
"""update.py — 源树状态入口

应用补丁 01-22 → 安装判据根 + GN 判据 examples。只管 chromium 源树一侧;
提取/构建另行显式执行 (select.py / build.py)。
成功后源树保持补丁态 (幂等, 重跑识别已应用跳过); 仅中途失败回滚。

用法: python tools/update.py <chromium src 路径>
"""
import glob
import os
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT_BUILD_GN = """group("default") {
  testonly = true
  deps = [
    ":liew_bridge",
    ":liew_bridge_recipe",
  ]
}

group("liew_bridge_recipe") {
  testonly = true
  deps = [ "//.liew/examples/views_smoke" ]
}

group("liew_bridge") {
  testonly = true
  deps = [
    "//base",
    "//base/test:test_support",
    "//components/viz/host",
    "//components/viz/service",
    "//mojo/core/embedder",
    "//third_party/perfetto/src/trace_processor:export_json",
    "//third_party/perfetto/src/trace_processor:storage_minimal",
    "//ui/aura",
    "//ui/base",
    "//ui/base/ime",
    "//ui/compositor",
    "//ui/compositor:test_support",
    "//ui/gfx",
    "//ui/gl",
    "//ui/resources:ui_test_pak",
    "//ui/views:views",
    "//ui/views:test_support",
    "//ui/wm",
  ]
}
"""

GN_EXAMPLES = ("views_smoke", "views_media_smoke")


def die(msg):
    print("[update] 错误: " + msg, file=sys.stderr)
    sys.exit(1)


def g(*args, cwd=None, check=True):
    r = subprocess.run(["git", *args], cwd=cwd,
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        die(f"git {' '.join(args[:2])} 失败: {r.stderr.strip()[:200]}")
    return r.returncode == 0


def main():
    if len(sys.argv) != 2:
        die("用法: python tools/update.py <chromium src 路径>")
    src = os.path.abspath(sys.argv[1])
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
    if not os.path.isdir(src):
        die(f"chromium 源树不存在: {src}")

    applied = []
    patches = sorted(glob.glob(os.path.join(repo, "patches", "[0-9]*.patch")))
    # --- 阶段 0: 栈式重放 —— 先逆序全卸, 再顺序全打。
    # 逐刀独立 reverse-check 会互相污染 (多刀碰同一文件时, 前刀的验证被
    # 后刀改动干扰); 逆序卸载与顺序应用天然成对, 才是正确的幂等语义。
    for p in reversed(patches):
        g("apply", "-R", "--ignore-whitespace", p, cwd=src, check=False)
    for p in patches:
        if not g("apply", "--check", "--ignore-whitespace", p, cwd=src,
                 check=False):
            die(f"{os.path.basename(p)} 前向校验失败 (上游变更冲突?), 手动处理后重试")
        g("apply", "--ignore-whitespace", p, cwd=src)
        applied.append(p)
        print(f"[update] 应用: {os.path.basename(p)}", flush=True)

    # --- 阶段 0b: submodule 补丁 (patches/submodule/<src 相对路径>/*.patch) ---
    sub_applied = []
    for p in sorted(glob.glob(os.path.join(
            repo, "patches", "submodule", "**", "*.patch"), recursive=True)):
        rel = os.path.relpath(p, os.path.join(repo, "patches", "submodule"))
        sub = os.path.dirname(rel)
        sub_dir = os.path.join(src, sub)
        if not os.path.exists(os.path.join(sub_dir, ".git")):
            die(f"{sub_dir} 不是 git 子仓")
        base = ["-C", sub_dir, "-c", "core.autocrlf=false", "apply"]
        # 同样栈式: 逆序卸 + 顺序打
        g(*base, "-R", "--ignore-whitespace", p, check=False)
        if not g(*base, "--check", "--ignore-whitespace", p, check=False):
            die(f"submodule 前向校验失败: {rel}")
        g(*base, "--ignore-whitespace", p)
        sub_applied.append((sub_dir, p))
        print(f"[update] submodule 应用: {rel}", flush=True)

    # --- 阶段 1: 判据根 + GN examples (成功保持补丁态; 仅失败回滚) ---
    bak = os.path.join(src, "BUILD.gn.vendor-bak")
    shutil.copy2(os.path.join(src, "BUILD.gn"), bak)
    succeeded = False
    try:
        with open(os.path.join(src, "BUILD.gn"), "w",
                  encoding="utf-8", newline="\n") as fh:
            fh.write(ROOT_BUILD_GN)
        dst_ov = os.path.join(src, ".liew", "examples")
        os.makedirs(dst_ov, exist_ok=True)
        src_ov = os.path.join(repo, "examples")
        for example in GN_EXAMPLES:
            example_src = os.path.join(src_ov, example)
            for base, _dirs, names in os.walk(example_src):
                for n in names:
                    sp = os.path.join(base, n)
                    dp = os.path.join(dst_ov, example,
                                      os.path.relpath(sp, example_src))
                    os.makedirs(os.path.dirname(dp), exist_ok=True)
                    shutil.copy2(sp, dp)

        succeeded = True
        print("[update] 完成 (源树保持补丁态)", flush=True)
        print("[update] 提取另行显式执行: select.py --src <SRC> --repo <目标树>")
        print(f"[update] 刷树进本仓 (维护者决策): select.py --src {src} --repo {repo}")
    finally:
        if not succeeded:
            shutil.move(bak, os.path.join(src, "BUILD.gn"))
            shutil.rmtree(os.path.join(src, ".liew", "examples"),
                          ignore_errors=True)
            for p in reversed(applied):   # 栈式: 逆序回滚
                g("apply", "-R", "--ignore-whitespace", p, cwd=src, check=False)
            for sub_dir, p in reversed(sub_applied):
                g("-C", sub_dir, "-c", "core.autocrlf=false",
                  "apply", "-R", "--ignore-whitespace", p, check=False)
            print("[update] 已回滚本次应用的补丁与根文件", flush=True)


if __name__ == "__main__":
    main()
