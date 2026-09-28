#pragma once
// ApplicationImpl — liew::Application 的装配实现(chrome_private 内部)。
//
// 装配契约:成员构造顺序 = 声明顺序,析构 = 严格逆序。这条 C++ 语言规则
// 完整托管启动/拆卸顺序 —— 禁止重排成员声明。各步骤的历史依据见对应层类
// 注释;有意不对称(mojo 无 Shutdown/线程池不停/分配器 NoDestructor)见
// ProcessRuntime/TaskRuntime 头注释。
//
// 边界规则:本头(及其传递引入的 chrome 树头)只准被 liew/app/ 的
// liew_app.cc 与 chrome_private 内部 include,不得进入任何框架公共头。

#include <memory>
#include <string>

#include "liew/app/liew_app.h"

#include "liew/chrome_private/ui/accessibility_platform.h"
#include "liew/chrome_private/render/compositor_runtime.h"
#include "liew/chrome_private/ui/liew_views_delegate.h"
#include "liew/chrome_private/runtime/process_runtime.h"
#include "liew/chrome_private/runtime/task_runtime.h"
#include "liew/chrome_private/ui/ui_toolkit.h"
#include "liew/chrome_private/app/window_manager.h"

namespace base {
class RunLoop;
}

namespace liew {

class Application::Impl {
  public:
    explicit Impl(AppConfig config);
    ~Impl();

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    int Run();
    void Quit();
    bool IsRunning() const;

    // 供 Window(chrome_private/app/window.cpp)经 friend 访问:
    internal::WindowManager* window_manager() { return window_manager_.get(); }
    const std::u16string& application_name() const { return application_name_; }

  private:
    // WindowManager 在每次关窗后回调(Unretained 安全:window_manager_ 是
    // 最末成员、最先析构,此刻 Impl 其余成员全部存活,且 run_loop_ 为 null)。
    void QuitIfAllWindowsClosed();

    // --- 装配契约:构造顺序 = 声明顺序,析构 = 严格逆序收尾。禁止重排。---
    std::u16string application_name_;  // 值成员;被下方两个 delegate 的初始化引用
    internal::ProcessRuntime process_;  // L0 进程地基(at_exit 为其首子成员)
    internal::TaskRuntime tasks_;       // L1 任务设施:UI 泵 + 线程池
    std::unique_ptr<internal::AccessibilityPlatform>
        accessibility_platform_;                                     // L1.5
    std::unique_ptr<CompositorRuntime> compositor_runtime_;         // L2 合成运行时
    internal::ScopedClipboard clipboard_;  // 钉位:RB 清理后/compositor 析构前销毁剪贴板
    internal::ScopedResourceBundle resource_bundle_;                // pak
    internal::ScopedAuraEnv aura_env_;  // L2→L3 唯一接缝:context_factory 注入
    internal::ScopedInputMethod input_method_;
    internal::ScopedViewsDelegate views_delegate_;
    internal::ScopedWMState wm_state_;
    internal::ScopedDesktopScreen desktop_screen_;

    // --- 会话层(最末) ---
    base::RunLoop* run_loop_ = nullptr;
    std::unique_ptr<internal::WindowManager> window_manager_;
};

}  // namespace liew
