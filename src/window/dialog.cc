#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "liew/widget/widgets/container.h"
#include "liew/window/dialog.h"
#include "src/widget/widget_impl.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/color/color_id.h"
#include "ui/views/background.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/dialog_delegate.h"

namespace liew {
namespace {

class ContentView final : public views::View {
  public:
    ContentView() {
        SetLayoutManager(std::make_unique<views::FillLayout>());
        SetBackground(views::CreateSolidBackground(ui::kColorWindowBackground));
    }
};

class DialogHost final : public views::DialogDelegate,
                         public views::WidgetObserver {
  public:
    DialogHost(DialogConfig config, liew::Widget* root)
        : title_(config.title) {
        auto contents = std::make_unique<ContentView>();
        liew::internal::WidgetBridge::MountToNativeHost(root, contents.get());
        SetContentsView(std::move(contents));
        modal_ = config.modal;
        // 无内置 OK/Cancel 按钮,内容全权归 RootView。
        SetButtons(0);
        SetButtonEnabled(ui::mojom::DialogButton::kOk, false);
        SetButtonEnabled(ui::mojom::DialogButton::kCancel, false);
    }

    // views::WidgetDelegate:
    std::u16string GetWindowTitle() const override { return title_; }
    bool CanResize() const override { return false; }
    ui::mojom::ModalType GetModalType() const override {
        return modal_ ? ui::mojom::ModalType::kWindow
                      : ui::mojom::ModalType::kNone;
    }

    // views::WidgetObserver: delegate 必须比 Widget 长命。
    // OnWidgetClosing 时 native 树尚活,先整棵拆除壳树;
    // OnWidgetDestroyed 后再延后一拍销毁 Impl 本体。
    void OnWidgetClosing(views::Widget* widget) override {
        if (on_closing)
            on_closing();
    }

    void OnWidgetDestroyed(views::Widget* widget) override {
        widget->RemoveObserver(this);
        if (on_destroyed)
            on_destroyed();
    }

    std::function<void()> on_closing;
    std::function<void()> on_destroyed;

  private:
    std::u16string title_;
    bool modal_ = true;
};

}  // namespace

class Dialog::Impl {
  public:
    std::unique_ptr<liew::Container> root;
    std::unique_ptr<DialogHost> delegate;
    views::Widget* widget = nullptr;
};

void Dialog::Show(Widget* anchor, DialogConfig config,
                  std::function<void(Widget* root)> fill) {
    CHECK(anchor != nullptr);

    auto* impl = new Impl;
    impl->root = std::make_unique<liew::Container>();
    impl->root->SetLayout(FilledLayout{});
    impl->delegate = std::make_unique<DialogHost>(config, impl->root.get());
    impl->delegate->on_closing = [impl] { impl->root.reset(); };
    impl->delegate->on_destroyed = [impl] {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce([](Impl* self) { delete self; }, impl));
    };

    views::View* anchor_native = internal::WidgetBridge::GetNative(anchor);
    impl->widget = views::DialogDelegate::CreateDialogWidget(
        impl->delegate.get(), nullptr,
        anchor_native->GetWidget()->GetNativeView());
    impl->widget->AddObserver(impl->delegate.get());

    const gfx::Size size(static_cast<int>(config.size.width()),
                         static_cast<int>(config.size.height()));
    impl->widget->SetSize(size);

    if (fill)
        fill(impl->root.get());
    impl->widget->Show();
}

}  // namespace liew
