#include "liew/chrome_private/app/window_manager.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/background.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace liew::internal {
namespace {

// 窗口内容视图: FillLayout + 主题色实心背景。自定义内容往
// widget->GetContentsView() 里 AddChildView 即可铺满客户区。
class ContentView : public views::View {
  public:
    ContentView() {
        SetLayoutManager(std::make_unique<views::FillLayout>());
        SetBackground(views::CreateSolidBackground(ui::kColorWindowBackground));
    }
};

// 每窗一个 WidgetDelegate。内容视图以 unique_ptr 显式移交给 View 树;
// delegate 与 Widget 则由 WindowManager 分别持有。
class WindowDelegate : public views::WidgetDelegate {
  public:
    WindowDelegate(std::u16string title, bool resizable)
        : title_(std::move(title)), resizable_(resizable) {
        SetContentsView(std::make_unique<ContentView>());
    }

    // views::WidgetDelegate:
    std::u16string GetWindowTitle() const override { return title_; }
    bool CanResize() const override { return resizable_; }

  private:
    std::u16string title_;
    bool resizable_;
};

}  // namespace

// 声明顺序是所有权约束:析构逆序,Widget 必须先于其 delegate 销毁。
struct WindowManager::WindowState {
    WindowId id = kInvalidWindowId;
    std::unique_ptr<WindowDelegate> delegate;
    std::unique_ptr<views::Widget> widget;
};

WindowManager::WindowManager(base::RepeatingClosure all_windows_closed)
    : all_windows_closed_(std::move(all_windows_closed)) {}

WindowManager::~WindowManager() {
    // map 清空时 WindowState 逆序析构:Widget 先死、delegate 后死。
    // ~Widget 可能经兜底路径同步重入 OnCloseRequested —— 回调目标与
    // map 同对象,已摘除的 id find 失败即早退,安全。
}

WindowId WindowManager::CreateWindow(WindowConfig config, const std::u16string& fallback_title) {
    CHECK_GT(config.size.width(), 0u);
    CHECK_GT(config.size.height(), 0u);

    const std::u16string title = config.title && config.title[0] ? config.title : fallback_title;

    const WindowId id = next_window_id_++;
    auto state = std::make_unique<WindowState>();
    state->id = id;
    state->delegate = std::make_unique<WindowDelegate>(title, config.property.isResizable);
    state->widget = std::make_unique<views::Widget>();

    views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    params.delegate = state->delegate.get();
    const gfx::Size size(static_cast<int>(config.size.width()),
                         static_cast<int>(config.size.height()));
    params.bounds = gfx::Rect(size);
    state->widget->Init(std::move(params));

    views::Widget* widget = state->widget.get();
    if (config.property.isCenter) {
        widget->CenterWindow(size);
    }
    // The close callback must target this same object that owns the map
    // (see header red-line note); ClosedReason is unused and swallowed here.
    widget->MakeCloseSynchronous(base::BindOnce(
        [](WindowManager* self, WindowId id, views::Widget::ClosedReason) {
            self->OnCloseRequested(id);
        },
        base::Unretained(this), id));
    windows_.emplace(id, std::move(state));
    widget->Show();
    return id;
}

bool WindowManager::CloseWindow(WindowId id) {
    auto it = windows_.find(id);
    if (it == windows_.end()) {
        return false;
    }
    it->second->widget->Close();
    return true;
}

void WindowManager::OnCloseRequested(WindowId id) {
    auto it = windows_.find(id);
    if (it == windows_.end()) {
        return;
    }

    auto state = std::move(it->second);
    windows_.erase(it);
    state.reset();
    all_windows_closed_.Run();
}

}  // namespace liew::internal
