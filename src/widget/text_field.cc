#include <functional>
#include <string>
#include <utility>

#include "liew/widget/widgets/text_field.h"
#include "src/widget/widget_impl.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"

namespace liew {
namespace {

class ControllerBridge final : public views::TextfieldController {
  public:
    std::function<void(const std::u16string&)> on_changed;

    // views::TextfieldController:
    void ContentsChanged(views::Textfield* sender,
                         const std::u16string& new_contents) override {
        if (on_changed)
            on_changed(new_contents);
    }
};

class LiewTextfield final : public views::Textfield {
  public:
    using views::Textfield::Textfield;

  private:
    // 输入学习是浏览器 embedder 的能力,liew 明确不参与。
    bool ShouldDoLearning() override { return false; }
};

}  // namespace

class TextField::Impl : public Widget::Impl {
  public:
    views::Textfield* field() { return static_cast<views::Textfield*>(native); }

    ControllerBridge controller;
    std::u16string text;
};

TextField::TextField() {
    impl_ = new TextField::Impl;
    auto* impl = static_cast<TextField::Impl*>(impl_);
    impl->native = new LiewTextfield;
    impl->field()->set_controller(&impl->controller);

    impl->field()->GetViewAccessibility().SetName(
        std::u16string(), ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
}

TextField::~TextField() = default;

void TextField::SetText(const char16_t* text) {
    static_cast<TextField::Impl*>(impl_)->field()->SetText(
        text != nullptr ? text : u"");
}

void TextField::SetPlaceholder(const char16_t* text) {
    auto* field = static_cast<TextField::Impl*>(impl_)->field();
    const bool has = text != nullptr && text[0] != u'\0';
    const std::u16string value = has ? std::u16string(text) : std::u16string();
    field->SetPlaceholderText(value);
    field->GetViewAccessibility().SetName(
        value, has ? ax::mojom::NameFrom::kPlaceholder
                   : ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
}

const char16_t* TextField::GetText() const {
    auto* impl = static_cast<TextField::Impl*>(impl_);
    impl->text = impl->field()->GetText();
    return impl->text.c_str();
}

void TextField::SetCallback(const TextFieldCallback& callback) {
    static_cast<TextField::Impl*>(impl_)->controller.on_changed =
        [fn = callback.on_changed](const std::u16string& text) {
            if (fn)
                fn(text.c_str());
        };
}

}  // namespace liew
