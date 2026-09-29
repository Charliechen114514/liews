#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "liew/widget/widgets/combobox.h"
#include "src/widget/widget_impl.h"
#include "ui/base/models/combobox_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/combobox/combobox.h"

namespace liew {
namespace {

class StringListModel final : public ui::ComboboxModel {
  public:
    // 拷贝而非 move:入参 buffer 来自调用方堆,move 会把跨堆指针带进 DLL。
    explicit StringListModel(const std::vector<std::u16string>& items)
        : items(items) {}

    // ui::ComboboxModel:
    size_t GetItemCount() const override { return items.size(); }
    std::u16string GetItemAt(size_t index) const override {
        return items[index];
    }

  private:
    std::vector<std::u16string> items;
};

}  // namespace

class Combobox::Impl : public Widget::Impl {
  public:
    views::Combobox* combobox() { return static_cast<views::Combobox*>(native); }

    void WillDestroyNative() override { subscription = {}; }

    std::function<void(int)> on_selected_changed;
    base::CallbackListSubscription subscription;
};

Combobox::Combobox(const std::vector<std::u16string>& items) {
    impl_ = new Combobox::Impl;
    auto* impl = static_cast<Combobox::Impl*>(impl_);
    impl->native =
        new views::Combobox(std::make_unique<StringListModel>(items));

    impl->combobox()->GetViewAccessibility().SetName(
        std::u16string(), ax::mojom::NameFrom::kAttributeExplicitlyEmpty);

    impl->subscription = impl->combobox()->AddSelectedIndexChangedCallback(
        base::BindRepeating(
            [](views::Combobox* box, std::function<void(int)>* callback) {
                if (*callback) {
                    const auto index = box->GetSelectedIndex();
                    (*callback)(index ? static_cast<int>(*index) : -1);
                }
            },
            impl->combobox(), &impl->on_selected_changed));
}

Combobox::~Combobox() = default;

int Combobox::GetSelectedIndex() const {
    const auto index =
        static_cast<Combobox::Impl*>(impl_)->combobox()->GetSelectedIndex();
    return index ? static_cast<int>(*index) : -1;
}

void Combobox::SetSelectedIndex(int index) {
    auto* box = static_cast<Combobox::Impl*>(impl_)->combobox();
    if (index < 0)
        box->SetSelectedIndex(std::nullopt);
    else
        box->SetSelectedIndex(static_cast<size_t>(index));
}

void Combobox::SetCallback(const ComboboxCallback& callback) {
    static_cast<Combobox::Impl*>(impl_)->on_selected_changed =
        callback.on_selected_changed;
}

}  // namespace liew
