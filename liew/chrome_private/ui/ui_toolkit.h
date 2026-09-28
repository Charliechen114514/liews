#pragma once
// UiToolkit — liew 框架的 UI 装配模块(L3)。
// 七个 Scoped 类:构造即 init、析构即 teardown;平台 ifdef 全部吸入各自
// 实现,组合点零分支。各对象在 Application::Impl 中的声明顺序即装配与
// 拆卸契约(尤其 ScopedClipboard 的钉位,见其注释)。

#include <memory>
#include <string_view>

#include "base/files/file_path.h"

#include "liew/base/class_macro.h"

namespace aura {
class Env;
}
namespace display {
class Screen;
}
namespace ui {
class ContextFactory;
}
namespace views {
class ViewsDelegate;
}
namespace wm {
class WMState;
}

namespace liew::internal {

// ResourceBundle pak 装载;析构 CleanupSharedInstance。
class ScopedResourceBundle final {
  public:
    struct Options {
        // Resource pak file name, resolved against DIR_ASSETS (exe dir);
        // only needs to outlive construction.
        std::string_view pak_name = "liew_resources.pak";
        // Explicit full path overrides pak_name when non-empty.
        base::FilePath pak_path;
    };

    explicit ScopedResourceBundle(const Options& options);
    LIEW_DISABLE_COPY_MOVE(ScopedResourceBundle);
    ~ScopedResourceBundle();
};

class ScopedAuraEnv final {
  public:
    explicit ScopedAuraEnv(ui::ContextFactory* context_factory);
    LIEW_DISABLE_COPY_MOVE(ScopedAuraEnv);
    ~ScopedAuraEnv();

  private:
    std::unique_ptr<aura::Env> env_;
};

// 输入法;析构 ShutdownInputMethod。
class ScopedInputMethod final {
  public:
    ScopedInputMethod();
    LIEW_DISABLE_COPY_MOVE(ScopedInputMethod);
    ~ScopedInputMethod();
};

// 持有注入的 ViewsDelegate(构造即自注册,析构自卸任)。
class ScopedViewsDelegate final {
  public:
    explicit ScopedViewsDelegate(std::unique_ptr<views::ViewsDelegate> delegate);
    LIEW_DISABLE_COPY_MOVE(ScopedViewsDelegate);
    ~ScopedViewsDelegate();

  private:
    std::unique_ptr<views::ViewsDelegate> delegate_;
};

// wm::WMState(USE_AURA 吸入实现)。
class ScopedWMState final {
  public:
    ScopedWMState();
    LIEW_DISABLE_COPY_MOVE(ScopedWMState);
    ~ScopedWMState();

  private:
    std::unique_ptr<wm::WMState> wm_state_;
};

// display::Screen(ENABLE_DESKTOP_AURA 吸入实现)。
class ScopedDesktopScreen final {
  public:
    ScopedDesktopScreen();
    LIEW_DISABLE_COPY_MOVE(ScopedDesktopScreen);
    ~ScopedDesktopScreen();

  private:
    std::unique_ptr<display::Screen> screen_;
};

// 钉位类:唯一职责是把 Clipboard::DestroyClipboardForCurrentThread 钉在
// 逆序析构的这一格 —— ResourceBundle 清理之后、CompositorRuntime 之前
// (精确复刻原手写 dtor 时序)。禁止挪动其在 Impl 中的声明位置。
class ScopedClipboard final {
  public:
    ScopedClipboard() = default;
    LIEW_DISABLE_COPY_MOVE(ScopedClipboard);
    ~ScopedClipboard();
};

} // namespace liew::internal
