#pragma once
// WindowManager — liew 框架窗口子系统的内部核。
// 拥有全部顶层窗口的 Widget/Delegate 生命周期与 WindowId 分配。
//
// 红线:窗口 map 与 MakeCloseSynchronous 关窗回调必须同对象 ——
// ~Widget 的兜底路径(override_close_)会同步重入回调,拆开即悬空。
//
// Self-defence: 同 liew_app.h(CreateWindow 方法名撞 Win32 宏)。
#ifdef CreateWindow
#    undef CreateWindow
#endif

#include <cstddef>
#include <map>
#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "liew/app/liew_app_config.h"

namespace views {
class Widget;
}

namespace liew {
class Widget;
}

namespace liew::internal {

class WindowManager final {
  public:
    // all_windows_closed: 每次有窗口关闭后回调(窗口集状态由宿主经
    // empty() 查询;RunLoop 去留决策归 Application,不归这里)。
    explicit WindowManager(base::RepeatingClosure all_windows_closed);

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;
    ~WindowManager();

    // Creates and immediately shows a top-level window.
    WindowId CreateWindow(WindowConfig config, const std::u16string& fallback_title);
    bool CloseWindow(WindowId id);

    // 每窗的内容根容器;窗口不存在时返回 nullptr。
    Widget* RootView(WindowId id);

    std::size_t WindowCount() const { return windows_.size(); }
    bool empty() const { return windows_.empty(); }

  private:
    struct WindowState; // delegate/widget 所有权束;完整定义在 .cpp

    void OnCloseRequested(WindowId id);

    base::RepeatingClosure all_windows_closed_;
    WindowId next_window_id_ = 1;
    std::map<WindowId, std::unique_ptr<WindowState>> windows_;
};

} // namespace liew::internal
