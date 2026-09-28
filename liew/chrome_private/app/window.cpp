// liew::Window 的实现(pimpl 住 chrome_private,公共头零 chrome 依赖)。
#include "liew/window/liew_window.h"

#include <utility>

#include "liew/chrome_private/app/application_impl.h"

namespace liew {

class Window::Impl {
  public:
    Impl(internal::WindowManager* manager, WindowId id) : manager_(manager), id_(id) {}

    internal::WindowManager* manager_;
    WindowId id_;
};

Window::Window(Application& application, WindowConfig config)
    : impl_(new Impl(application.impl_->window_manager(), kInvalidWindowId)) {
    // title 为空时由应用名兜底(WindowManager::CreateWindow 的 fallback 参数)。
    impl_->id_ = impl_->manager_->CreateWindow(std::move(config),
                                               application.impl_->application_name());
}

Window::~Window() {
    // 若窗口已被用户关闭,CloseWindow 找不到 id 返回 false,空壳 no-op;
    // 若仍在,析构即关窗 —— 最后一窗消失经统一关闭管道触发应用退出。
    std::ignore = impl_->manager_->CloseWindow(impl_->id_);
    delete impl_;
}

void Window::Close() {
    std::ignore = impl_->manager_->CloseWindow(impl_->id_);
}

}  // namespace liew
