#pragma once
// ProcessRuntime — liew 框架的进程地基模块(L0)。
// 一次性初始化进程级全局:AtExit / CommandLine / OLE(Win) / FeatureList(Win)
// / mojo core / ICU / discardable 分配器 / 字体。
// 全部 adopt-or-create:宿主已初始化的就收养,支持嵌入已有环境。
//
// 有意不对称(不要"补全"):mojo core 无 Shutdown;FeatureList/ICU/字体无
// 销毁;discardable 分配器为 NoDestructor。析构只收尾 OLE 与 AtExit。

#include <cstddef>
#include <memory>

namespace liew::internal {

class ProcessRuntime final {
  public:
    struct Options {
        int argument_count = 0;
        const char* const* argument_params = nullptr;
        // Discardable budget for decoded-image and similar caches.
        std::size_t discardable_budget_bytes = 32u << 20;
    };

    explicit ProcessRuntime(const Options& options);
    ProcessRuntime(const ProcessRuntime&) = delete;
    ProcessRuntime& operator=(const ProcessRuntime&) = delete;
    ~ProcessRuntime();

  private:
    struct State;  // at_exit/ole 等,完整定义在 .cpp
    std::unique_ptr<State> state_;
};

}  // namespace liew::internal
