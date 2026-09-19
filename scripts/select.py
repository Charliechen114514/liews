#!/usr/bin/env python3
"""select.py — 从 Chromium checkout 提取 //ui/views 闭包, vendor 到本仓库.

v0 算法 (ninja 真值 + owning-dir 闭包):
  1. ninja -t graph <root-output> 递归吐出构建 DAG (只含真实编译/codegen 边)
  2. 从节点标签收集"被编译的源文件" (.cc/.cpp/.rs/.rc/...)
  3. 每个源文件向上找到最近的含 BUILD.gn 的目录 → owning dir
     (例外: third_party 根的 BUILD.gn 是聚合器不是归属边界, 见 owning_dirs)
  4. 去掉被祖先覆盖的条目, 对剩余 owning dir 整目录递归拷贝
  5. 收集 LICENSE 族文件 → THIRD_PARTY_LICENSES.md
  6. 全量 sha256 → MANIFEST.json (漂移检测基准)
"""
import argparse
import hashlib
import json
import os
import posixpath
import re
import shutil
import stat
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

NODE_RE = re.compile(r'^"[0-9A-F]+" \[label="([^"]*)"')
SRC_RE = re.compile(r".(cc|cpp|cxx|c|mm|m|rs|rc|S|asm|def|proto|py|pydeps|json|gn|gni|mojom|template|typemap|js|stamp|bat|sh|plist|gradle|toml|lock)$|^DEPS$|OWNERS$|LICENSE")
SKIP_DIRS = {".git", "__pycache__", "out"}
# 上游组件自带的 .gitattributes (如 angle/rust crate 的 "* text=auto") 优先级高于
# 本仓库根 .gitattributes, 会在 add/checkout 时做行尾归一化 → blob 与工作树字节
# 漂移, MANIFEST 报假警。它们是上游仓库的 git 配置, 对本仓库无意义, 一律不带,
# 让根 "* -text" 全面接管 (字节保真)。
SKIP_FILES = {".gitattributes", ".gitignore"}

# 每个 target 都隐含用到、但不出现在编译文件映射里的基建目录
# testing 只留 gtest/gmock: 整目录会把 libfuzzer 的 5.4 万个微型种子文件拖进来
# (真实数据仅 23MB, exFAT 大簇上虚占 6GB+; 2026-09-13 实测事故)
ALWAYS_INCLUDE = [
    "build",
    "build_overrides",
    "buildtools",
    "tools/grit",
    "tools/gritsettings",
    "testing/gtest",
    "testing/gmock",
    # 刀15 E2E 回灌 (2026-09-15): import 级依赖 (gni/BUILD.gn/数据文件) 不在
    # 编译闭包内, gen-fix loop 实测 90+ 缺口归并的目录清单.
    "mojo",  # 根 BUILD.gn + public/tools bindings 生成器链
    "tools/metrics", "tools/i18n", "tools/protoc_wrapper", "tools/l10n",
    "third_party/wtl",  # 刀16/e2e: 纯头库 (is_win dep of views_examples)
    # 刀16/e2e 三轮: viz/service 的 dep, 无编译源纯组目录 + 头 (common_export.h)
    "third_party/blink/public/common",
    "third_party/blink/public/platform",  # 刀18/ultimate: media traits 引 web_fullscreen_video_status.h
    # 刀16/e2e: 构建期代码生成工具 (ninja action 的工具输入, 非 编译源)
    "tools/json_to_struct",
    "tools/variations",
    # 刀16/e2e: 纯头库盲区 (mojo core include ipcz/ipcz.h, 闭包无编译源)
    "third_party/ipcz",
    # 刀16/e2e: webrtc -I 指自身根, 头跨子目录互引; owning-dir 闭包系统性
    # 漏纯头子目录 (dcsctp/common, modules/include 等) — 整树收录 (test 自动裁)
    "third_party/webrtc",
    # 刀18/ultimate: 跨目录头引用 (编译期 include 路径覆盖) —
    # owning-dir 闭包看不见这些纯头/数据子树, 编译时才报缺.
    "third_party/ffmpeg",          # media filters include libavutil/*.h
    "third_party/fp16",            # media renderers include fp16.h
    "third_party/google_benchmark", # cc/benchmarks include benchmark/*.h
    "third_party/crashpad/crashpad/third_party/zlib",  # crashpad zlib_crashpad.h
    "device/bluetooth",            # bluetooth mojom traits include bluetooth*.h
    "components/ukm",              # cc/test include test_ukm_recorder.h
    "ui/ozone/public",             # views test include ozone_platform.h

    "media/ffmpeg",  # 刀18
    "third_party/khronos",  # 刀18
    "gpu/GLES2",  # 刀18
    "tools/win", "tools/ipc_fuzzer", "tools/json_schema_compiler",
    # 刀22: tools/code_coverage/typescript/polymer 出局 — webui 遗族, 全闭包图零引用
    "components/vector_icons",
    # 刀23: components/signin 目录出局 (.ninja_deps 终审 0 命中);
    # features.gni 以文件级保留在 FILE_EXTRAS (ui/color→ui/webui:buildflags
    # 加载链顶层 import 它)。注意: ALWAYS_INCLUDE 是目录清单, 文件路径进来
    # 会被 copy_tree 当目录 walk 而静默空拷 — 第一版修正就错在这。
    "components/gwp_asan/buildflags", "components/discardable_memory/public/mojom",
    "components/viz", "components/vrp_flags",
    "ui/strings", "ui/resources", "ui/qt",  # 刀18: ui/webui 出局 (拖 node/d3/stylelint 全家, 非 views 产品需求)
    "gpu/vulkan", "device/vr/buildflags", "extensions/buildflags",
    "third_party/jinja2", "third_party/markupsafe", "third_party/ply",
    "third_party/jni_zero", "third_party/perfetto",
    "third_party/inspector_protocol", "third_party/isimpledom",
    "third_party/iaccessible2", "third_party/abseil-cpp",
    "third_party/metrics_proto", "third_party/dawn/scripts",
    "third_party/llvm-libc/src",  # libc++ from_chars 需要 fp_bits.h (无编译源, submodule)
    "third_party/vulkan-headers/src",  # gpu/config vulkan.h 编译期头 (44MB, 纯头无源)
    "third_party/crashpad/crashpad/build",
]

# 已知泄漏: owning-dir 过拷带进来的工具链/测试/数据巨物 (v1 文件级闭包后复查)
VENDOR_EXCLUDE = {
    "third_party/devtools-frontend",     # Chrome DevTools 前端, 与 views 无关
    "third_party/depot_tools",           # gclient 脚本 checkout (vs_toolchain 引用)
    "third_party/catapult",              # 性能遥测框架
    "third_party/apache-windows-arm64",  # 测试用 Apache 二进制
    "third_party/hunspell_dictionaries",  # 拼写字典数据
    "third_party/crossbench-web-tests",
    "third_party/enterprise_companion",
    "third_party/node",                   # 刀18: webui 构建工具链, 非 views 闭包 (378M)
    "third_party/swiftshader",            # 刀18: dawn 软件后端 (dawn_use_swiftshader=false, 540M)
    "third_party/llvm-build",            # hermetic clang 工具链二进制
    "third_party/rust-toolchain",        # rustc/cargo 工具链二进制
    "third_party/compiler-rt",           # 整树 4836 文件, 闭包只需 atomic 1 个 .c (见 FILE_EXTRAS)
}

# owning-dir 整目录拷贝会把"单文件依赖"放大成整树的组件, 对这些只按文件补
# (compiler-rt:atomic 是 Rust std 的 builtins, 三个工具链变体都在编它)
FILE_EXTRAS = [
    "third_party/compiler-rt/BUILD.gn",
    "third_party/compiler-rt/README.chromium",
    "third_party/compiler-rt/src/lib/builtins/assembly.h",
    "third_party/compiler-rt/src/lib/builtins/atomic.c",
    "third_party/compiler-rt/src/lib/builtins/int_endianness.h",
    # 刀16/e2e (2026-09-17): views_examples 闭包 gen 一次过的急加载缺口
    # (gapfill 两轮日志反查; 根级 BUILD.gn 走文件级防整树爆炸;
    # crubit 类已被 patch17 (USE_CBOR_RUST 门) 消化, 不列).
    "components/components_strings.grd.gritdeps",
    "components/guest_view/buildflags/buildflags.gni",
    "components/proto_extras/proto_extras.gni",
    "components/startup_metric_utils/BUILD.gn",
    "components/strings/BUILD.gn",
    "components/system_media_controls/linux/buildflags/BUILD.gn",
    "content/public/common/features.gni",
    "gpu/BUILD.gn",
    "media/BUILD.gn",
    "media/media_options.gni",
    "pdf/features.gni",
    "printing/buildflags/buildflags.gni",
    "third_party/closure_compiler/closure_args.gni",
    "third_party/closure_compiler/compile_js.gni",
    "third_party/crashpad/crashpad/BUILD.gn",
    "third_party/d3/BUILD.gn",
    "third_party/eigen3/BUILD.gn",
    "third_party/ffmpeg/ffmpeg_options.gni",
    "third_party/fp16/BUILD.gn",
    "third_party/fxdiv/BUILD.gn",
    "third_party/gemmlowp/BUILD.gn",
    "third_party/khronos/BUILD.gn",
    "third_party/libprotobuf-mutator/fuzzable_proto_library.gni",
    "third_party/llvm-libc/BUILD.gn",
    "third_party/ml_dtypes/BUILD.gn",
    "third_party/neon_2_sse/BUILD.gn",
    "third_party/perfetto/include/perfetto/test/BUILD.gn",
    "third_party/perfetto/src/tracing/test/BUILD.gn",
    "third_party/webrtc/BUILD.gn",
    "third_party/webrtc/api/test/network_emulation/BUILD.gn",
    "third_party/webrtc/test/network/BUILD.gn",
    "third_party/webrtc/webrtc.gni",
    "third_party/widevine/cdm/BUILD.gn",
    "third_party/windows_app_sdk_headers/BUILD.gn",
    "tools/generate_stubs/rules.gni",
    "tools/json_comment_eater/json_comment_eater.py",
    "tools/json_to_struct/json_to_struct.gni",
    "tools/v8_context_snapshot/v8_context_snapshot.gni",
    "v8/gni/split_static_library.gni",
    "v8/gni/v8.gni",
    # 刀16/e2e 续 (E:/e2e2 二轮验证): 此前被中断的整目录拷贝掩埋的缺口.
    "chromeos/components/libsegmentation/buildflags.gni",
    "chrome/VERSION",  # version.py 输入 (刀18: 原整目录 ALWAYS_INCLUDE 过拷 477M)
    # 刀18: gpu 根级纯头 (被全 gpu 头文件引用)
    "gpu/gpu_export.h",
    "gpu/gpu_gles2_export.h",
    "gpu/gpu_util_export.h",
    "gpu/raster_export.h",
    "mojo/public/tools/fuzzers/mojolpm.gni",
    "components/optimization_guide/features.gni",
    "third_party/blink/public/BUILD.gn",
    "third_party/crashpad/crashpad/third_party/mini_chromium/BUILD.gn",
    "media/mojo/BUILD.gn",
    "third_party/crashpad/crashpad/third_party/zlib/BUILD.gn",
    "components/system_media_controls/linux/buildflags/buildflags.gni",
    "third_party/widevine/cdm/widevine.gni",
    "third_party/webrtc/modules/BUILD.gn",
    "third_party/webrtc/modules/utility/BUILD.gn",
    "third_party/webrtc/net/dcsctp/common/BUILD.gn",
    "gpu/webgpu/dawn_commit_hash.h",  # 刀18/ultimate: gpu_host_impl 引用
    # testing/ 根已不入 ALWAYS_INCLUDE, 这两个 gni 被 base/BUILD.gn 等闭包内
    # 必加载文件顶层 import (fuzzer_test.gni 又 import test.gni), 缺一 gen 即炸;
    # testing/ 其余 (libfuzzer 54K 种子 + gtest/gmock 自家 test 树) 均不带.
    "testing/libfuzzer/fuzzer_test.gni",
    "testing/test.gni",
    "third_party/webrtc/api/test/simulated_network.h",  # webrtc network_queue 引用
    "chrome/app/theme/chromium/BRANDING",
    # 构建工具: gn 随树分发 — 但不需列此: buildtools 整目录 (ALWAYS_INCLUDE)
    # 的 walk 天然带上 win/gn.exe (曾在此显式列出, 与目录拷双重命中,
    # 目录拷保持只读位后 FILE_EXTRAS 再写即 PermissionError, 2026-09-20 实弹)。
    # ninja 是通用工具, 走系统 PATH。
    "components/signin/features.gni",  # 刀23 修正续: ui/webui 顶层 import (①层 gen 依赖)
    "gpu/webgpu/DAWN_VERSION",
    "third_party/blink/public/public_features.gni",
    # 刀19: node 全裁 — patch19 断 ui_test_pak→mojo/public/js:resources 边后,
    # mojo/public/js/BUILD.gn 脱离加载图, node.gni 的最后一个 import 方消失。
    # (optimize_webui=false 时图内零 node() 实例化, 三轮构建日志 0 命中佐证;
    #  若日后重开 webui/加 node() 目标, 缺口会在 gen/ninja 期显式报错, 届时再回补)
    "third_party/webrtc/test/BUILD.gn",
    "ui/webui/BUILD.gn",
    "ui/webui/webui_features.gni",
    "ui/webui/resources/tools/generate_code_cache.gni",
    "ui/webui/resources/tools/generate_grd.gni",
]

# 刀16/e2e: grit 资源数据 — ninja 期 action 输入, 不在编译闭包.
# 模式化目录 (整目录拷, 含 .grd/.grdp/.gritdeps/.xtb):
#   components/strings, ash/strings, chrome/app/resources (全 .xtb);
#   components 根级的散装 .grd/.grdp/.gritdeps 见下方文件清单.
GRIT_DATA_DIRS = [
    "components/strings",
    # 刀22 修正: resources/terms 恢复 — components_locale_settings.grd 内部
    # <include file="resources/terms/..."> 是 grit 运行时解析 (build.ninja 只
    # 署名 .grd 本体, 内部引用全隐形)。GRD 内部引用只能扫 .grd 内容或靠
    # 干净 E2E, 路径 grep 判不了。
    "components/resources/terms",
    # 刀21: "chrome/app/resources" 摘除 — 537 个 .xtb (chrome 自家翻译, 128MB)
    # 全闭包图零消费者 (chrome grd 不在本闭包, 已过 build 到 13858 步实证)。
    # 刀22: "ash/strings" 摘除 — ash grd 不在闭包。
]

GRIT_DATA_FILES = [
    # 刀16/e2e: components 根级 grit 资源 (ninja 期枚举收敛后固化)
    "components/android_system_error_page_strings.grdp",
    "components/arc_strings.grdp",
    "components/autofill_payments_strings.grdp",
    "components/autofill_strings.grdp",
    "components/blocked_content_strings.grdp",
    "components/bookmark_bar_strings.grdp",
    "components/bookmark_component_strings.grdp",
    "components/browsing_data_strings.grdp",
    "components/collaboration_strings.grdp",
    "components/commerce_strings.grdp",
    "components/components_chromium_strings.grd",
    "components/components_google_chrome_strings.grd",
    "components/components_locale_settings.grd",
    "components/components_settings_strings.grdp",
    "components/components_strings.grd",
    "components/components_strings.grd.gritdeps",
    "components/components_variant_public_strings.grd",
    "components/compose_strings.grdp",
    "components/contextual_cueing_strings.grdp",
    "components/contextual_tasks_strings.grdp",
    "components/crash_strings.grdp",
    "components/dialog_strings.grdp",
    "components/dom_distiller_strings.grdp",
    "components/enterprise_strings.grdp",
    "components/error_page_strings.grdp",
    "components/external_intents_strings.grdp",
    "components/facilitated_payments_strings.grdp",
    "components/find_in_page_strings.grdp",
    "components/flags_strings.grdp",
    "components/fullscreen_control_strings.grdp",
    "components/global_media_controls_strings.grdp",
    "components/heavy_ad_intervention_strings.grdp",
    "components/history_clusters_strings.grdp",
    "components/history_strings.grdp",
    "components/javascript_dialogs_strings.grdp",
    "components/live_caption_strings.grdp",
    "components/login_dialog_strings.grdp",
    "components/management_strings.grdp",
    "components/media_message_center_strings.grdp",
    "components/new_or_sad_tab_strings.grdp",
    "components/omnibox_pedal_ui_strings.grdp",
    "components/omnibox_strings.grdp",
    "components/page_info_strings.grdp",
    "components/paint_preview_strings.grdp",
    "components/password_manager_strings.grdp",
    "components/payments_strings.grdp",
    "components/pdf_strings.grdp",
    "components/permissions_strings.grdp",
    "components/personal_context_strings.grdp",
    "components/policy_strings.grdp",
    "components/print_media_strings.grdp",
    "components/printing_component_strings.grdp",
    "components/privacy_sandbox_chrome_strings.grdp",
    "components/privacy_sandbox_strings.grd",
    "components/protocol_handler_strings.grdp",
    "components/reset_password_strings.grdp",
    "components/saved_tab_groups_strings.grdp",
    "components/search_engine_choice_strings.grdp",
    "components/security_interstitials_strings.grdp",
    "components/send_tab_to_self_strings.grdp",
    "components/site_settings_strings.grdp",
    "components/smart_tab_sharing.grdp",
    "components/ssl_errors_strings.grdp",
    "components/subresource_filter_strings.grdp",
    "components/supervised_user_strings.grdp",
    "components/sync_ui_strings.grdp",
    "components/tab_groups_strings.grdp",
    "components/tab_resume_strings.grdp",
    "components/translate_strings.grdp",
    "components/undo_strings.grdp",
    "components/user_data_importer_strings.grdp",
    "components/user_education_strings.grdp",
    "components/version_ui_strings.grdp",
    "components/wallet_strings.grdp",
    "components/webapps_strings.grdp",
    "components/webxr_strings.grdp",
]

FILE_EXTRAS += GRIT_DATA_FILES

# 第三方子依赖的测试/基准/fuzz 目录: 闭包内零编译源, 纯数据肥肉, 一律不带
# (2026-09-13 实测: angle 内嵌 VK-GL-CTS 1.7GB, expat testdata 128MB,
#  sqlite test+fuzz 144MB, v8/test 71MB, skia tests/site/gm/... ~100MB)
# 一方代码 (ui/base/...) 的 test 目录同样裁剪 — 根 target //ui/views:views 不含
# 任何 testonly 依赖, test 目录/test 源文件均为死重; 唯一例外是 base/test/
# (check_is_test.{h,cc} 被 feature_list.cc 生产引用, scoped_logging_settings.h
# 被 logging.cc 引用) 与 testing/ (gtest/gmock + test.gni import 面).
# GN 惰性加载保证安全: testonly target 的 BUILD.gn 从根不可达则不会被解析.
PRUNE_NAME = {
    "test", "tests", "testdata", "test_data",
    "fuzz", "fuzzers", "fuzzing",
    "benchmark", "benchmarks",
    "glmark2", "VK-GL-CTS",
}
PRUNE_PATH = {
    "third_party/skia/site",
    "third_party/skia/gm",
    "third_party/skia/resources",
    "third_party/skia/infra",
    "third_party/skia/platform_tools",
    "third_party/skia/bazel",
    # 刀20 已回滚: dawn/tools/golang (362MB) 是 tint 代码生成的构建期工具链 —
    # generate-sources-gn.py 在 ACTION 里 runtime spawn go.exe (脚本内部
    # subprocess), 对 build.ninja 路径 grep / 构建日志 / .ninja_deps 三重隐形。
    # "图零引用"是真观察、错结论: runtime spawn 不留静态痕迹, 只有干净 E2E
    # 能逮住 (2026-09-19 用户轮 9385 步即炸于此)。同性质构建工具: nasm。
    # 刀24: 只留宿主实际会选的半边 — get_cipd_platform() 取当前宿主 os-arch
    # (cipd_deps.py:35, 构造上不可达另一半), x64 机器裁 windows-arm64 (179MB)。
    # (arm64 Windows 宿主构建时需回补)
    "third_party/dawn/tools/golang/windows-arm64",
}

# 一方 test 目录白名单 (路径前缀, 不裁):
# 刀16/e2e (2026-09-16): views_examples 闭包的 test_support 是生产依赖
# (ninja 图反查 17 目录), 一并白名单.
TEST_DIR_WHITELIST = (
    "base/test",
    # 刀18: "testing" 整目录白名单撤销 — gtest 自身的 test/ 子树可裁 (54K 文件).
    # 编译所需部分由 ALWAYS_INCLUDE (testing/gtest, testing/gmock) + FILE_EXTRAS 覆盖.

    "base/task/sequence_manager/test",
    "cc/test",
    "cc/benchmarks",  # 刀18: benchmark_instrumentation 是生产代码
    "components/viz/test",
    "ui/aura/test",
    "ui/base/test",  # 刀16/e2e 修正: 此前漏列 (三轮 E2E 反复撞的 .cc 缺口根因)
    "ui/base/clipboard/test",
    "ui/compositor/test",
    "ui/display/test",
    "ui/display/win/test",
    "ui/events/test",
    "ui/gfx/animation/keyframe/test",
    "ui/gfx/geometry/test",
    "ui/gfx/test",
    "ui/gl/test",
    "ui/views/animation/test",
    "ui/views/corewm/test",
    "ui/views/test",
    "third_party/webrtc/api/test",
    "third_party/google_benchmark/src/include/benchmark",  # PRUNE_NAME "benchmark" 误杀修复
    "third_party/webrtc/stats/test",
    "third_party/webrtc/test",
    "third_party/webrtc/test/network",
)

# 散装测试源 (不处在 test 目录内的 *_unittest.cc / *_test.cc / *_perftest.cc
# / *_browsertest.cc) — 生产假阳性例外 (以 _test.cc 结尾但为生产代码):
BARE_TEST_RE = re.compile(
    r"(_unittest|_perftest|_browsertest|_test)\.cc$")
BARE_TEST_KEEP = {
    "base/check_is_test.cc",
    "ui/base/hit_test.cc",
    "base/task/sequence_manager/test/sequence_manager_for_test.cc",
    "base/test/power_monitor_test.cc",
    "cc/test/layer_tree_pixel_resource_test.cc",
    "cc/test/layer_tree_pixel_test.cc",
    "cc/test/layer_tree_test.cc",
    "cc/test/pixel_test.cc",
    "cc/test/test_layer_tree_frame_sink.cc",
    "components/viz/test/begin_frame_args_test.cc",
    "components/viz/test/begin_frame_source_test.cc",
    "components/viz/test/delegated_ink_point_renderer_skia_for_test.cc",
    "components/viz/test/test_compositor_frame_sink.cc",
    "components/viz/test/test_frame_sink_manager.cc",
    "ui/accessibility/platform/ax_platform_for_test.cc",
    "ui/base/interaction/interactive_test.cc",
    "ui/compositor/test/draw_waiter_for_test.cc",
    "ui/views/test/dialog_test.cc",
    "ui/views/test/focus_manager_test.cc",
    "ui/views/test/widget_test.cc",
    "ui/views/test/widget_test_aura.cc",
}


def prune_dir(rel_dir):
    """rel_dir: 相对源树根的 posix 目录路径; 返回 True 则整棵子树不拷贝."""
    if rel_dir in PRUNE_PATH:
        return True
    parts = rel_dir.split("/")
    if parts[0] in ("third_party", "v8"):
        # 刀18/final: third_party 的 test 目录也查白名单 (webrtc test 基础设施)
        # 刀18/ultimate: ALWAYS_INCLUDE 子树内的 PRUNE_NAME 命中也放行
        # (google_benchmark/src/include/benchmark 被 "benchmark" 误杀)
        if any(n in PRUNE_NAME for n in parts):
            return not rel_dir.startswith(TEST_DIR_WHITELIST)
        return False
    # 一方代码: test 命名目录同样裁, 白名单除外.
    if any(n in PRUNE_NAME for n in parts):
        return not rel_dir.startswith(TEST_DIR_WHITELIST)
    return False


def prune_file(rel_file):
    """散装测试源文件级裁剪 (目录级裁不到的)."""
    return bool(BARE_TEST_RE.search(posixpath.basename(rel_file))) \
        and rel_file not in BARE_TEST_KEEP

ROOT_EXTRAS = {"LICENSE": "LICENSE", "DEPS": "DEPS.chromium"}


def compiled_sources(ninja, out_dir, root_outputs):
    """stream 解析 ninja -t graph, 产出 checkout 相对路径的源文件集合."""
    cmd = [ninja, "-C", out_dir, "-t", "graph"] + list(root_outputs)
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True,
                            encoding="utf-8", errors="replace")
    seen = set()
    for line in proc.stdout:
        m = NODE_RE.match(line)
        if not m:
            continue
        label = m.group(1)
        if label.startswith("../../") and SRC_RE.search(label):
            seen.add(posixpath.normpath(label[len("../../"):]))
    if proc.wait() != 0:
        sys.exit(f"[select] ninja -t graph 失败 (exit={proc.returncode})")
    return seen


def owning_dirs(src_root, files):
    """每个源文件向上归到最近的含 BUILD.gn 的目录.

    例外: third_party 根的 BUILD.gn 是聚合器, 不是归属边界. 典型事故:
    libc++ 上游源码 third_party/libc++/src/src/*.cpp 内部无 BUILD.gn,
    归属升到 third_party 根导致整树 40+GB 拷贝. 此类强制归属一级组件目录.
    """
    owners = set()
    for f in files:
        d = posixpath.dirname(f)
        while d:
            if d == "third_party":
                parts = posixpath.dirname(f).split("/")
                d = "/".join(parts[:2]) if len(parts) >= 2 else ""
                break
            if os.path.isfile(os.path.join(src_root, d, "BUILD.gn")):
                break
            d = posixpath.dirname(d)
        if d:
            owners.add(d)
    return owners


def reduce_to_outermost(dirs):
    """去掉被祖先覆盖的目录 (拷祖先时递归带走)."""
    kept = []
    for d in sorted(dirs):
        if not any(k != d and d.startswith(k + "/") for k in kept):
            kept.append(d)
    return kept


def dir_stats(path):
    """子树文件数与字节数 (排除 SKIP_DIRS/.pyc); dry-run 统计裁剪体积用."""
    fc = sz = 0
    for base, dirs, names in os.walk(path):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for n in names:
            if not n.endswith(".pyc"):
                fc += 1
                try:
                    sz += os.path.getsize(os.path.join(base, n))
                except OSError:
                    pass
    return fc, sz


def copy_tree(s, t, label, dry=False):
    """递归拷贝; size+mtime 一致的既有文件跳过 (重跑快进 + 自愈被杀进程的截断文件).

    dry=True: 只统计 (将拷/将跳/裁剪子树的文件数与字节数), 不写任何文件.
    """
    n_copy = n_skip = b_copy = 0
    prunes = []
    for base, dirs, names in os.walk(s):
        rel = os.path.relpath(base, s).replace("\\", "/")
        full = label if rel == "." else f"{label}/{rel}"
        keep, dropped = [], []
        for x in dirs:
            if x in SKIP_DIRS:
                continue
            if prune_dir(f"{full}/{x}"):
                dropped.append(x)
            else:
                keep.append(x)
        dirs[:] = keep
        for x in dropped:
            p = f"{full}/{x}"
            fc, sz = dir_stats(os.path.join(base, x)) if dry else (0, 0)
            prunes.append((p, fc, sz))
        tbase = t if rel == "." else os.path.join(t, *rel.split("/"))
        if not dry:
            os.makedirs(tbase, exist_ok=True)
        for name in names:
            if name.endswith(".pyc") or name in SKIP_FILES:
                continue
            # 散装测试源文件级裁剪 (目录级裁不到的生产目录里的 *_unittest.cc 等)
            if prune_file(f"{full}/{name}"):
                continue
            sp, tp = os.path.join(base, name), os.path.join(tbase, name)
            try:
                if (os.path.getsize(sp) == os.path.getsize(tp)
                        and abs(os.path.getmtime(sp) - os.path.getmtime(tp)) < 2):
                    n_skip += 1
                    continue
            except OSError:
                pass
            if dry:
                n_copy += 1
                b_copy += os.path.getsize(sp)
                print(f"       [dry-cp] {full}/{name}", flush=True)
            else:
                shutil.copy2(sp, tp)
                n_copy += 1
    return n_copy, n_skip, b_copy, prunes


def copy_vendor(src_root, repo, dirs, dry=False):
    tot_copy = tot_skip = tot_prune = 0
    all_prunes = []
    for i, d in enumerate(dirs, 1):
        s = os.path.join(src_root, *d.split("/"))
        t = os.path.join(repo, *d.split("/"))
        n_copy, n_skip, b_copy, prunes = copy_tree(s, t, d, dry)
        tot_copy += n_copy
        tot_skip += n_skip
        tot_prune += sum(sz for _, _, sz in prunes)
        all_prunes += prunes
        mb = f" ({b_copy / 1e6:.1f} MB)" if dry and n_copy else ""
        extra = f" / 裁 {len(prunes)}" if prunes else ""
        print(f"       [{i}/{len(dirs)}] {d}: 拷 {n_copy}{mb} / 跳 {n_skip}{extra}",
              flush=True)
    if not dry:
        for rel in FILE_EXTRAS:
            t = os.path.join(repo, *rel.split("/"))
            os.makedirs(os.path.dirname(t), exist_ok=True)
            try:
                shutil.copy2(os.path.join(src_root, *rel.split("/")), t)
            except PermissionError:
                # 已存在的只读目标 (如目录 walk 先拷的只读工具): 清只读位重试
                os.chmod(t, stat.S_IWRITE)
                shutil.copy2(os.path.join(src_root, *rel.split("/")), t)
        for s_name, t_name in ROOT_EXTRAS.items():
            shutil.copy2(os.path.join(src_root, s_name), os.path.join(repo, t_name))
    else:
        for rel in FILE_EXTRAS:
            print(f"       [dry-cp] {rel}", flush=True)
        for s_name, t_name in ROOT_EXTRAS.items():
            print(f"       [dry-cp] {s_name} -> {t_name}", flush=True)
    return tot_copy, tot_skip, tot_prune, all_prunes


def collect_licenses(repo, dirs):
    hits = []
    for d in dirs:
        walk_root = os.path.join(repo, *d.split("/"))
        for base, subdirs, names in os.walk(walk_root):
            rel = os.path.relpath(base, repo).replace("\\", "/")
            subdirs[:] = [x for x in subdirs if not prune_dir(f"{rel}/{x}")]
            for n in names:
                if (n.startswith("LICENSE") or n.startswith("COPYING")
                        or n == "README.chromium"):
                    hits.append(os.path.relpath(os.path.join(base, n), repo))
    for rel in FILE_EXTRAS:
        fn = os.path.basename(rel)
        if fn.startswith("LICENSE") or fn.startswith("COPYING") or fn == "README.chromium":
            hits.append(rel)
    for n in os.listdir(repo):  # 根级 (LICENSE 等)
        if n.startswith("LICENSE") or n.startswith("COPYING") or n == "README.chromium":
            hits.append(n)
    hits = sorted(set(hits))
    out = os.path.join(repo, "THIRD_PARTY_LICENSES.md")
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("# Third-party licenses bundled in this vendor snapshot\n\n")
        fh.write("Each path below keeps its original license file intact.\n\n")
        for h in hits:
            fh.write(f"- `{h}`\n")
    return len(hits)


def write_manifest(repo, dirs, revision):
    files = {}
    total = 0
    extras = {os.path.normpath(os.path.join(repo, *rel.split("/"))) for rel in FILE_EXTRAS}
    for d in dirs:
        walk_root = os.path.join(repo, *d.split("/"))
        for base, subdirs, names in os.walk(walk_root):
            rel = os.path.relpath(base, repo).replace("\\", "/")
            subdirs[:] = [x for x in subdirs if not prune_dir(f"{rel}/{x}")]
            for n in names:
                p = os.path.join(base, n)
                rel = os.path.relpath(p, repo).replace("\\", "/")
                h = hashlib.sha256()
                with open(p, "rb") as fh:
                    for chunk in iter(lambda: fh.read(1 << 20), b""):
                        h.update(chunk)
                files[rel] = [os.path.getsize(p), h.hexdigest()]
                total += files[rel][0]
    for p in sorted(extras):
        if p in files:
            continue
        rel = os.path.relpath(p, repo).replace("\\", "/")
        h = hashlib.sha256()
        with open(p, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
        files[rel] = [os.path.getsize(p), h.hexdigest()]
        total += files[rel][0]
    manifest = {
        "chromium_revision": revision,
        "vendor_dirs": dirs,
        "file_count": len(files),
        "total_bytes": total,
        "files": files,
    }
    with open(os.path.join(repo, "MANIFEST.json"), "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=1, sort_keys=True)
    return len(files), total


def ensure_graph(src, out, gn):
    """在源树上 (重)生成闭包判据元数据 —— 判据保鲜是提取自身的职责。

    工具链全用源树自带 (llvm-build / rust-toolchain), 与本机安装无关;
    gen 用 --script-executable=python 指系统 python (源树 .gn 的 python3
    在 Windows 上通常是 Store 占位符)。
    """
    root_gn = os.path.join(src, "BUILD.gn")
    with open(root_gn, encoding="utf-8") as fh:
        root = fh.read()
    if "//examples/views_media_smoke" not in root:
        sys.exit("[select] 源树根 BUILD.gn 未含 //examples/* 判据目标 "
                 "(E: 侧状态由 update.sh 维护: 应用补丁 + 换装根 BUILD.gn + examples)")

    repo_args = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             os.pardir, "args.gn")
    rust_root = os.path.join(src, "third_party", "rust-toolchain")
    rustc = os.path.join(rust_root, "bin", "rustc.exe")
    ver = subprocess.run([rustc, "--version"], capture_output=True,
                         text=True, encoding="utf-8").stdout
    m = re.search(r"rustc (\d[\d.]*-nightly) \(([0-9a-f]{8})", ver)
    if not m:
        sys.exit(f"[select] 无法解析源树 rustc 版本: {ver.strip()}")
    rustc_version = f"{m.group(1)}-{m.group(2)}"

    with open(repo_args, encoding="utf-8") as fh:
        body = fh.read()
    body += ("\n# --- 本机注入 (select.py ensure_graph; 全部派生自 --src) ---\n"
             f'clang_base_path = "{os.path.join(src, "third_party", "llvm-build", "Release+Asserts")}"\n'
             f'rustc_version = "{rustc_version}"\n'
             f'rust_sysroot_absolute = "{rust_root}"\n'
             f'rust_bindgen_root = "{rust_root}"\n')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "args.gn"), "w", encoding="utf-8", newline="") as fh:
        fh.write(body)

    env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="0")
    r = subprocess.run([gn, "gen", out, "--script-executable=python"],
                       cwd=src, env=env)
    if r.returncode != 0:
        sys.exit(f"[select] gn gen 失败 (exit={r.returncode})")


def stage_root(repo, script_repo):
    """目标树缺的根文件/examples 从本仓补齐 —— 调用方零仪式 (启动前置内化)。

    只补缺失, 绝不覆盖已有 (幂等; 提取回本仓时 repo==script_repo 天然 no-op)。
    """
    staged = []
    # 根文件 = 仓库正本, 总是覆盖 (改版后旧树根不会再吃陈版); 记录实际变化
    for f in ("BUILD.gn", ".gn", ".gitignore", "lock.json"):
        dst = os.path.join(repo, f)
        sp = os.path.join(script_repo, f)
        if not os.path.isfile(sp):
            continue
        os.makedirs(repo, exist_ok=True)
        if not os.path.isfile(dst) or \
                open(dst, 'rb').read() != open(sp, 'rb').read():
            shutil.copy2(sp, dst)
            staged.append(f)
    src_ov = os.path.join(script_repo, "examples")
    dst_ov = os.path.join(repo, "examples")
    if os.path.isdir(src_ov):
        for base, _dirs, names in os.walk(src_ov):
            rel = os.path.relpath(base, src_ov)
            dbase = dst_ov if rel == "." else os.path.join(dst_ov, rel)
            for n in names:
                sp, dp = os.path.join(base, n), os.path.join(dbase, n)
                if not os.path.isfile(dp):
                    os.makedirs(dbase, exist_ok=True)
                    shutil.copy2(sp, dp)
                    r = os.path.relpath(dp, dst_ov).replace("\\", "/")
                    staged.append(f"examples/{r}")
    return staged


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="chromium src checkout")
    ap.add_argument("--out", default=None,
                    help="gen 元数据目录; 缺省 = <src>/out/E2EGraph 且每次自动 (重)生成 "
                         "(显式传入 = 专家模式, 原样使用不重生成)")
    ap.add_argument("--repo", required=True, help="vendor 目标仓库 (D:\\Projects\\views-standalone)")
    ap.add_argument("--root-outputs", nargs="+", required=True)
    ap.add_argument("--gn", default=None, help="gn.exe; 缺省 = <src>/buildtools/win/gn.exe")
    ap.add_argument("--ninja", default="ninja")
    ap.add_argument("--no-hash", action="store_true", help="跳过 sha256 (快速迭代)")
    ap.add_argument("--dry-run", action="store_true",
                    help="只报告将拷贝/跳过/裁剪的内容与体积, 不写任何文件")
    args = ap.parse_args()

    script_repo = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
    lock_path = os.path.join(args.repo, "lock.json")
    if not os.path.isfile(lock_path):
        lock_path = os.path.join(script_repo, "lock.json")  # dry-run 借用同源
    with open(lock_path, encoding="utf-8") as fh:
        revision = json.load(fh)["chromium"]["revision"]

    if not args.dry_run and os.path.abspath(args.repo) != script_repo:
        staged = stage_root(args.repo, script_repo)
        if staged:
            print(f"[select] 铺根 (缺什么补什么): {', '.join(staged)}", flush=True)

    if args.out is None:
        args.out = os.path.join(args.src, "out", "E2EGraph")
        args.gn = args.gn or os.path.join(args.src, "buildtools", "win", "gn.exe")
        print(f"[select] 0/5 gen 闭包元数据 (源树自动保鲜): {args.out}", flush=True)
        ensure_graph(args.src, args.out, args.gn)

    print(f"[select] 1/5 解析 ninja 图: {args.root_outputs}", flush=True)
    files = compiled_sources(args.ninja, args.out, args.root_outputs)
    print(f"       编译源文件: {len(files)}", flush=True)

    print("[select] 2/5 计算 owning-dir 闭包", flush=True)
    owners = owning_dirs(args.src, files)
    dirs = reduce_to_outermost(
    owners | set(ALWAYS_INCLUDE) | set(GRIT_DATA_DIRS))
    dropped = [d for d in dirs if d in VENDOR_EXCLUDE]
    dirs = [d for d in dirs if d not in VENDOR_EXCLUDE]
    if dropped:
        print(f"       [排除] 泄漏目录: {dropped}", flush=True)
    print(f"       owning dirs: {len(owners)} -> 外层 {len(dirs)}", flush=True)

    print("[select] 3/5 拷贝 vendor 目录" + (" [DRY-RUN]" if args.dry_run else ""),
          flush=True)
    tot_copy, tot_skip, prune_bytes, all_prunes = copy_vendor(
        args.src, args.repo, dirs, dry=args.dry_run)
    if args.dry_run:
        print(f"       汇总: 将拷 {tot_copy} / 跳 {tot_skip} / "
              f"裁剪子树 {len(all_prunes)} 个 共 {prune_bytes / 1e6:.1f} MB", flush=True)
        print("       裁剪体积 top20:", flush=True)
        for p, fc, sz in sorted(all_prunes, key=lambda x: -x[2])[:20]:
            print(f"         {sz / 1e6:9.1f} MB  {fc:6d} 文件  {p}", flush=True)

    print("[select] 4/5 汇总 LICENSE", flush=True)
    if args.dry_run:
        print("       (dry-run, 跳过)", flush=True)
    else:
        n_lic = collect_licenses(args.repo, dirs)
        print(f"       许可证文件: {n_lic} 个 → THIRD_PARTY_LICENSES.md", flush=True)

    print("[select] 5/5 MANIFEST", flush=True)
    if args.dry_run or args.no_hash:
        print("       (跳过)", flush=True)
    else:
        fc, tb = write_manifest(args.repo, dirs, revision)
        print(f"       {fc} 文件, {tb / 1e6:.1f} MB → MANIFEST.json", flush=True)
    print("[select] 完成.", flush=True)


if __name__ == "__main__":
    main()
