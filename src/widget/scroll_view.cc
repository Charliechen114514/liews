#include <memory>
#include <utility>

#include "base/check.h"
#include "liew/widget/widgets/scroll_view.h"
#include "src/widget/widget_impl.h"
#include "ui/views/controls/scroll_view.h"

namespace liew {

class ScrollView::Impl : public Widget::Impl {
  public:
    views::ScrollView* scroll() {
        return static_cast<views::ScrollView*>(native);
    }
};

ScrollView::ScrollView() {
    impl_ = new ScrollView::Impl;
    impl_->native = new views::ScrollView;
}

ScrollView::~ScrollView() = default;

Widget* ScrollView::SetContentImpl(std::unique_ptr<Widget> content) {
    CHECK(content != nullptr);
    ClearChildren();

    auto* raw = content.get();
    views::View* native = internal::WidgetBridge::TakeNativeOwnership(raw);
    static_cast<ScrollView::Impl*>(impl_)->scroll()->SetContents(
        base::WrapUnique(native));
    internal::WidgetBridge::AdoptChildShell(this, std::move(content),
                                            native->parent());
    return raw;
}

}  // namespace liew
