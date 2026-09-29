#include <functional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "liew/widget/widgets/button.h"
#include "src/widget/widget_impl.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/font_list.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"

namespace liew {
namespace {

class StyleButton final : public views::LabelButton {
  public:
    using views::LabelButton::LabelButton;

    views::Label* label() { return views::LabelButton::label(); }
};

SkColor ToSkColor(const Color& color) {
    return SkColorSetARGB(color.a, color.r, color.g, color.b);
}

}  // namespace

class Button::Impl : public Widget::Impl {
  public:
    views::LabelButton* button() {
        return static_cast<views::LabelButton*>(native);
    }

    StyleButton* styled() { return static_cast<StyleButton*>(native); }

    std::u16string text;
    std::function<void()> on_click;
};

Button::Button(const char16_t* text) {
    impl_ = new Button::Impl;
    auto* impl = static_cast<Button::Impl*>(impl_);
    impl->text = text != nullptr ? text : u"";
    impl->native = new StyleButton({}, impl->text);
}

Button::~Button() = default;

void Button::SetText(const char16_t* text) {
    auto* impl = static_cast<Button::Impl*>(impl_);
    impl->text = text != nullptr ? text : u"";
    impl->button()->SetText(impl->text);
}

const char16_t* Button::GetText() const {
    return static_cast<Button::Impl*>(impl_)->text.c_str();
}

void Button::SetCallback(const ButtonCallback& callback) {
    auto* impl = static_cast<Button::Impl*>(impl_);
    impl->on_click = callback.on_click;
    impl->button()->SetCallback(base::BindRepeating(
        [](std::function<void()>* click) {
            if (*click)
                (*click)();
        },
        &impl->on_click));
}

void Button::SetStyle(ButtonStyle style) {
    auto* styled = static_cast<Button::Impl*>(impl_)->styled();

    const auto& text = style.text_style;
    if (text.color) {
        styled->SetEnabledTextColors(ui::ColorVariant(ToSkColor(*text.color)));
    }

    const bool want_font = text.size.has_value() || text.weight.has_value() ||
                           text.bold || text.italic || text.underline ||
                           text.strikeout;
    if (want_font) {
        const gfx::FontList fonts = styled->label()->font_list();
        int font_style = fonts.GetFontStyle();
        if (text.italic)
            font_style |= gfx::Font::ITALIC;
        else
            font_style &= ~gfx::Font::ITALIC;
        if (text.underline)
            font_style |= gfx::Font::UNDERLINE;
        else
            font_style &= ~gfx::Font::UNDERLINE;
        if (text.strikeout)
            font_style |= gfx::Font::STRIKE_THROUGH;
        else
            font_style &= ~gfx::Font::STRIKE_THROUGH;

        const int size_delta = text.size ? *text.size - fonts.GetFontSize() : 0;
        gfx::Font::Weight weight = fonts.GetFontWeight();
        if (text.bold)
            weight = gfx::Font::Weight::BOLD;
        if (text.weight)
            weight = static_cast<gfx::Font::Weight>(*text.weight);

        styled->label()->SetFontList(
            fonts.Derive(size_delta, font_style, weight));
    }

    const auto& box = style.box_style;
    if (box.background) {
        const auto color = ui::ColorVariant(ToSkColor(*box.background));
        if (box.corner_radius) {
            styled->SetBackground(
                views::CreateRoundedRectBackground(color, *box.corner_radius));
        } else {
            styled->SetBackground(views::CreateSolidBackground(color));
        }
    }
    if (box.padding) {
        const Insets& padding = *box.padding;
        gfx::Insets insets;
        insets.set_top(padding.top());
        insets.set_left(padding.left());
        insets.set_bottom(padding.bottom());
        insets.set_right(padding.right());
        styled->SetBorder(views::CreateEmptyBorder(insets));
    }

    if (style.image_label_spacing) {
        styled->SetImageLabelSpacing(*style.image_label_spacing);
    }
}

}  // namespace liew
