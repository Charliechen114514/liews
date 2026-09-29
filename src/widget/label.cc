#include <string>
#include <utility>

#include "liew/widget/widgets/label.h"
#include "src/widget/widget_impl.h"
#include "ui/color/color_variant.h"
#include "ui/views/controls/label.h"

namespace liew {

class Label::Impl : public Widget::Impl {
  public:
    views::Label* label() { return static_cast<views::Label*>(native); }

    std::u16string text;
};

Label::Label(const char16_t* text) {
    impl_ = new Label::Impl;
    auto* impl = static_cast<Label::Impl*>(impl_);
    impl->text = text != nullptr ? text : u"";
    impl->native = new views::Label(impl->text);
}

Label::~Label() = default;

void Label::SetText(const char16_t* text) {
    auto* impl = static_cast<Label::Impl*>(impl_);
    impl->text = text != nullptr ? text : u"";
    impl->label()->SetText(impl->text);
}

const char16_t* Label::GetText() const {
    return static_cast<Label::Impl*>(impl_)->text.c_str();
}

void Label::SetStyle(LabelStyle style) {
    auto* label = static_cast<Label::Impl*>(impl_)->label();
    const auto& text = style.text_style;

    if (text.color) {
        label->SetEnabledColor(
            ui::ColorVariant(internal::ToSkColor(*text.color)));
    }

    const bool want_font = text.size.has_value() || text.weight.has_value() ||
                           text.bold || text.italic || text.underline ||
                           text.strikeout;
    if (want_font) {
        label->SetFontList(internal::DeriveFontList(label->font_list(), text));
    }
}

}  // namespace liew
