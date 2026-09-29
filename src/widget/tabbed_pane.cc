#include <memory>
#include <string>
#include <utility>

#include "base/check.h"
#include "liew/widget/widgets/tabbed_pane.h"
#include "src/widget/widget_impl.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane.h"

namespace liew {

class TabbedPane::Impl : public Widget::Impl {
  public:
    views::TabbedPane* tabs() {
        return static_cast<views::TabbedPane*>(native);
    }
};

TabbedPane::TabbedPane() {
    impl_ = new TabbedPane::Impl;
    impl_->native = new views::TabbedPane;
}

TabbedPane::~TabbedPane() = default;

Widget* TabbedPane::AddTabImpl(const char16_t* title,
                               std::unique_ptr<Widget> page) {
    CHECK(page != nullptr);

    auto* raw = page.get();
    views::View* native = internal::WidgetBridge::TakeNativeOwnership(raw);
    static_cast<TabbedPane::Impl*>(impl_)->tabs()->AddTab(
        std::u16string(title != nullptr ? title : u""),
        base::WrapUnique(native));
    internal::WidgetBridge::AdoptChildShell(this, std::move(page),
                                            native->parent());
    return raw;
}

int TabbedPane::GetSelectedIndex() const {
    return static_cast<int>(
        static_cast<TabbedPane::Impl*>(impl_)->tabs()->GetSelectedTabIndex());
}

void TabbedPane::SelectTabAt(int index) {
    CHECK_GE(index, 0);
    static_cast<TabbedPane::Impl*>(impl_)->tabs()->SelectTabAt(
        static_cast<size_t>(index));
}

}  // namespace liew
