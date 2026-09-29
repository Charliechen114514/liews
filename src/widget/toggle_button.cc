#include <functional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "liew/widget/widgets/toggle_button.h"
#include "src/widget/widget_impl.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/toggle_button.h"

namespace liew {

class ToggleButton::Impl : public Widget::Impl {
  public:
    views::ToggleButton* toggle() {
        return static_cast<views::ToggleButton*>(native);
    }

    std::function<void(bool)> on_toggled;
};

ToggleButton::ToggleButton() {
    impl_ = new ToggleButton::Impl;
    auto* impl = static_cast<ToggleButton::Impl*>(impl_);
    impl->native = new views::ToggleButton({});

    // 无文字的可聚焦控件必须显式声明无障碍名,否则 a11y 绘制检查 DCHECK。
    impl->toggle()->GetViewAccessibility().SetName(
        std::u16string(), ax::mojom::NameFrom::kAttributeExplicitlyEmpty);

    impl->toggle()->SetCallback(base::BindRepeating(
        [](views::ToggleButton* toggle, std::function<void(bool)>* callback) {
            if (*callback)
                (*callback)(toggle->GetIsOn());
        },
        impl->toggle(), &impl->on_toggled));
}

ToggleButton::~ToggleButton() = default;

bool ToggleButton::IsOn() const {
    return static_cast<ToggleButton::Impl*>(impl_)->toggle()->GetIsOn();
}

void ToggleButton::SetOn(bool on) {
    static_cast<ToggleButton::Impl*>(impl_)->toggle()->SetIsOn(on);
}

void ToggleButton::SetCallback(const ToggleButtonCallback& callback) {
    static_cast<ToggleButton::Impl*>(impl_)->on_toggled = callback.on_toggled;
}

}  // namespace liew
