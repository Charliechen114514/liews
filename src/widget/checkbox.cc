#include <functional>
#include <string>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "liew/widget/widgets/checkbox.h"
#include "src/widget/widget_impl.h"
#include "ui/views/controls/button/checkbox.h"

namespace liew {

class Checkbox::Impl : public Widget::Impl {
  public:
    views::Checkbox* checkbox() {
        return static_cast<views::Checkbox*>(native);
    }

    void WillDestroyNative() override { subscription = {}; }

    std::u16string text;
    std::function<void(bool)> on_checked_changed;
    base::CallbackListSubscription subscription;
};

Checkbox::Checkbox(const char16_t* text) {
    impl_ = new Checkbox::Impl;
    auto* impl = static_cast<Checkbox::Impl*>(impl_);
    impl->text = text != nullptr ? text : u"";
    impl->native = new views::Checkbox(impl->text);

    impl->subscription = impl->checkbox()->AddCheckedChangedCallback(
        base::BindRepeating(
            [](views::Checkbox* box, std::function<void(bool)>* callback) {
                if (*callback)
                    (*callback)(box->GetChecked());
            },
            impl->checkbox(), &impl->on_checked_changed));
}

Checkbox::~Checkbox() = default;

bool Checkbox::IsChecked() const {
    return static_cast<Checkbox::Impl*>(impl_)->checkbox()->GetChecked();
}

void Checkbox::SetChecked(bool checked) {
    static_cast<Checkbox::Impl*>(impl_)->checkbox()->SetChecked(checked);
}

void Checkbox::SetCallback(const CheckboxCallback& callback) {
    static_cast<Checkbox::Impl*>(impl_)->on_checked_changed =
        callback.on_checked_changed;
}

}  // namespace liew
