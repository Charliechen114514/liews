#include <functional>
#include <string>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "liew/widget/widgets/radio_button.h"
#include "src/widget/widget_impl.h"
#include "ui/views/controls/button/radio_button.h"

namespace liew {

class RadioButton::Impl : public Widget::Impl {
  public:
    views::RadioButton* radio() {
        return static_cast<views::RadioButton*>(native);
    }

    void WillDestroyNative() override { subscription = {}; }

    std::u16string text;
    std::function<void(bool)> on_checked_changed;
    base::CallbackListSubscription subscription;
};

RadioButton::RadioButton(const char16_t* text) {
    impl_ = new RadioButton::Impl;
    auto* impl = static_cast<RadioButton::Impl*>(impl_);
    impl->text = text != nullptr ? text : u"";
    impl->native = new views::RadioButton(impl->text);

    impl->subscription = impl->radio()->AddCheckedChangedCallback(
        base::BindRepeating(
            [](views::Checkbox* box, std::function<void(bool)>* callback) {
                if (*callback)
                    (*callback)(box->GetChecked());
            },
            impl->radio(), &impl->on_checked_changed));
}

RadioButton::~RadioButton() = default;

bool RadioButton::IsChecked() const {
    return static_cast<RadioButton::Impl*>(impl_)->radio()->GetChecked();
}

void RadioButton::SetChecked(bool checked) {
    static_cast<RadioButton::Impl*>(impl_)->radio()->SetChecked(checked);
}

void RadioButton::SetCallback(const CheckboxCallback& callback) {
    static_cast<RadioButton::Impl*>(impl_)->on_checked_changed =
        callback.on_checked_changed;
}

}  // namespace liew
