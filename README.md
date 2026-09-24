# liews: A Wrapped Seperated Wrapper Of Chrominum UI Views with Media

Liews 是笔者自己从 Chromium 版本抽取的 `ui/views` 独立 UI 工具包。目的是为了将这样一个框架从Chromium中剥离出来，用于构建本地native的框架。关于 views 的进一步细节，可以见指南：[Chromium自己对Views的介绍](https://www.chromium.org/chromium-os/developer-library/guides/views/intro/)。

> 提示的是，出于许可对齐，本仓库一样采用BSD-3-Clause的许可发布。

## 快速开始

> 1. 请注意，这个仓库非常庞大，在笔者本地的磁盘上，源码体积达到了惊人的约2.4G左右，请您注意网络的好可访问性。从而防止拉取时出现波动造成下行带宽的浪费！
> 2. 目前为止，我们只在 Windows11 x64 上验证了冒烟可行性，笔者没法在Linux下做出编译和可冒烟的承诺！

### 从环境配置开始

笔者将的确需要的 chrominum 的树内依赖通过patch + args.gn指定的方式干掉了，并且如下的测试验证通过了剥离，从而不需要进入树内可以用自己的环境进行分发。

| 工具                | 安装                                                | 说明                                                 |
| ------------------- | --------------------------------------------------- | ---------------------------------------------------- |
| Visual Studio 2022+ | VS Installer，勾「C++ 桌面开发」+ Windows SDK       | 本机 MSVC 工具链                                     |
| Python 3.11+        | [python.org](https://python.org) 安装并加入 PATH    | `python` 命令可用；Microsoft Store 占位符不算        |
| LLVM (含 clang-cl)  | [官方安装器](https://llvm.org/releases/)，加入 PATH | 我们强烈建议**自定义装到无空格路径**（如 `C:\LLVM`） |
| Rust (rustup)       | [rustup.rs](https://rustup.rs)                      | `rustc`/`cargo` 在 PATH                              |
| Ninja               | `winget install Ninja.Ninja`                        | depot_tools 的 shim 不算                             |
| bindgen             | `cargo install bindgen-cli`                         | 装完落在 `~/.cargo/bin`（rustup PATH 已含）          |

Rust 是现代 Chrominum 无法剥离的依赖，笔者深感遗憾无法将Rust摘出依赖链，所以您需要安装Rust和需要的依赖。目前，如下的配置是经过笔者验证可以编译的：

```shell
rustup toolchain install nightly-2026-06-16-x86_64-pc-windows-msvc
rustup default nightly-2026-06-16-x86_64-pc-windows-msvc
rustup component add rustfmt     # bindgen 生成的 bindings 要跑格式化
cargo install bindgen-cli        # C/Rust 绑定生成器
```

### 编译

```powershell
git clone https://github.com/Charliechen114514/liews.git && cd liews
cmake -B build
cmake --build build
# 流水线: ui.views 树(GN+ninja) -> liew/examples(CMake 编译/链接) -> compdb -> deploy
# 在Windows上需要编译大约20000组依赖; 生成器由你的环境/CMake Tools 决定
```

架构边界：**ui.views/ 树保持 GN**（`scripts/build.py build` 驱动）；**自研代码（liew/、examples/ 新增项）由仓库根 CMake 编译**。`build.py bridge` 从 GN 产物提取同源编译口径与静态链接闭包，闭包只链接一次形成单体 `liew.dll`；consumer 和 examples 只链接小型 import library。树内 `views_smoke` 保留为显式诊断目标，不再进入日常默认链接。

单步直行：`python scripts\build.py [build|bridge|compdb|deploy]`（`--target` 缺省即仓库内 ui.views 树）。

> 构建输出目录在仓库根 `build/Release`（GN 树外 out），部署集在 `build/deploy`。

### IDE（VSCode）

`.vscode/` 已配好（CMake Tools：`cmake.configureOnOpen`）。编译数据库 `build/compile_commands.json` 是**合并库**：CMake 侧条目（configure 时生成）+ 树内条目（views 全闭包，`build.py compdb` 并入，树内 examples 拷贝路径已重映射回仓库正本），供任意消费 compile_commands 的工具使用；单步 `python scripts\build.py compdb` 可不触发全量编译先补齐树内条目。注意 CMake 每次重新 configure 会重写该文件为其自身条目，`cmake --build` 流水线的 compdb 步骤会自动重新并入树内条目。

### 把 liew 当作 SDK 消费（find_package）

`cmake -B build` 之后，`build/cmake/` 里即有导出包（SDK = 本仓库检出 + 本 build 目录，绝对路径烘焙，不做安装/重定位）：

```cmake
cmake_minimum_required(VERSION 3.23)
project(myapp LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)  # 树内对象是 /MT，消费端必须对齐
set(liew_DIR "E:/liews/build/cmake")           # 或把 E:/liews/build 并入 CMAKE_PREFIX_PATH
find_package(liew CONFIG REQUIRED)             # 顶层 CMakeLists、先于 add_subdirectory
add_executable(myapp main.cc)
target_link_libraries(myapp PRIVATE liew::liew)
liew_copy_runtime_assets(myapp)                # POST_BUILD 拷树内运行期资源（dll/pak/icudtl）
```

### Advanced

如果您对仓库拖着2.4G的源码依赖极端不满，且恰好本地有 Chromium 源码树时，可以单独下载 patches + scripts/ 下脚本，您自行对您的 Chromium 树进行裁剪。笔者采集的commit是3649926a0bb43e258427a6d50c174fcaa4e339ae，您可以自行checkout到153版本，或者请自己的LLM对着patches + scripts/下的裁剪方式进行定制。方案如下：

```powershell
# 请您决策好一个期望的chrominum源码树，以笔者本地为例子, 可以执行
# git reset --hard 3649926a
# 如果您无法回到这个commit, 请检查自己的chrome树是否早于 2026 年 7 月 28 日的提交，如果是请更新自己的chrominum树源码
# 以及需要注意third_party下各依赖干净无脏提交
# update将会作用笔者裁剪好的patch作用到源码树上
python scripts\update.py <chromium src 路径>

# 使用select将会把打好patch的源码树拷贝到自己独立的目录下
# liews 就是这样诞生的 :)
python scripts\select.py --src <chromium src> --repo <新树目录> --root-outputs views_examples views_smoke views_media_smoke --no-hash

# 构建新树，如果您愿意，此时自己的源chrominum树就可以git reset --hard 到自己其他的commit上了:)
python scripts\build.py build --target <新树目录>
```

## 许可

- 本仓库原创部分（`scripts/`、`patches/`、`examples/`、`args.gn` 及根目录构建/打包文件）以 **BSD-3-Clause** 发布，见 [LICENSE](LICENSE)
- `ui.views/` 为 Chromium 裁剪树，遵循 Chromium 的 BSD-3-Clause 许可，见[ui.views/LICENSE](ui.views/LICENSE)；再分发须保留其版权与许可声明，且不得以 Google LLC / Chromium 名义为衍生品背书
- 树内第三方库保持各自原始许可，清单见[ui.views/THIRD_PARTY_LICENSES.md](ui.views/THIRD_PARTY_LICENSES.md)。注意 `third_party/ffmpeg` 含 LGPL-2.1 代码：静态链接发布二进制时需提供可重链接素材（object files 或源码），或改用动态链接
