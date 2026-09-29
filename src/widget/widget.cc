#include "src/widget/widget_impl.h"

#include <string>

#include "base/check.h"
#include "liew/widget/widgets/container.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"

namespace liew {

Widget::Widget() : impl_(nullptr) {}

Widget::~Widget() {
    impl_->children.clear();
    if (impl_->native != nullptr) {
        impl_->WillDestroyNative();
        if (impl_->native_host != nullptr)
            impl_->native_host->RemoveChildViewT(impl_->native).reset();
        else
            delete impl_->native;
    }
    delete impl_;
}

void* Widget::operator new(std::size_t size) {
    return ::operator new(size);
}

void Widget::operator delete(void* ptr) noexcept {
    ::operator delete(ptr);
}

void Widget::AddChildImpl(std::unique_ptr<Widget> child) {
    CHECK(child != nullptr);
    CHECK(child.get() != this);
    CHECK(impl_ != nullptr && impl_->native != nullptr);
    CHECK(child->impl_ != nullptr && child->impl_->native != nullptr);
    CHECK(child->impl_->native_host == nullptr);

    child->impl_->native_host = impl_->native;
    impl_->native->AddChildViewRaw(child->impl_->native);
    impl_->children.push_back(std::move(child));
}

void Widget::ClearChildren() {
    CHECK(impl_ != nullptr);
    impl_->children.clear();
}

void Widget::SetAccessibleName(const char16_t* name) {
    CHECK(impl_ != nullptr && impl_->native != nullptr);
    const bool has_name = name != nullptr && name[0] != u'\0';
    impl_->native->GetViewAccessibility().SetName(
        has_name ? std::u16string(name) : std::u16string(),
        has_name ? ax::mojom::NameFrom::kAttribute
                 : ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
}

void Widget::CloseWindow() {
    CHECK(impl_ != nullptr && impl_->native != nullptr);
    views::Widget* widget = impl_->native->GetWidget();
    CHECK(widget != nullptr);
    widget->Close();
}

void Widget::SetLayout(FilledLayout) {
    CHECK(impl_ != nullptr && impl_->native != nullptr);
    impl_->native->SetLayoutManager(std::make_unique<views::FillLayout>());
}

void Widget::SetLayout(BoxLayout layout) {
    CHECK(impl_ != nullptr && impl_->native != nullptr);
    const auto orientation = layout.orientation == Orientation::Vertical
                                 ? views::BoxLayout::Orientation::kVertical
                                 : views::BoxLayout::Orientation::kHorizontal;
    gfx::Insets insets;
    insets.set_top(layout.padding.top());
    insets.set_left(layout.padding.left());
    insets.set_bottom(layout.padding.bottom());
    insets.set_right(layout.padding.right());
    impl_->native->SetLayoutManager(std::make_unique<views::BoxLayout>(
        orientation, insets, layout.spacing));
}

Container::Container() {
    impl_ = new Widget::Impl;
    impl_->native = new views::View;
}

Container::~Container() = default;

namespace internal {

void WidgetBridge::MountToNativeHost(Widget* widget, views::View* host_native) {
    CHECK(widget != nullptr && widget->impl_ != nullptr);
    CHECK(widget->impl_->native != nullptr);
    CHECK(widget->impl_->native_host == nullptr);
    CHECK(host_native != nullptr);

    widget->impl_->native_host = host_native;
    host_native->AddChildViewRaw(widget->impl_->native);
    host_native->InvalidateLayout();
}

views::View* WidgetBridge::GetNative(const Widget* widget) {
    return widget->impl_->native;
}

views::View* WidgetBridge::TakeNativeOwnership(Widget* widget) {
    CHECK(widget != nullptr && widget->impl_ != nullptr);
    CHECK(widget->impl_->native != nullptr);
    CHECK(widget->impl_->native_host == nullptr);
    return widget->impl_->native;
}

void WidgetBridge::AdoptChildShell(Widget* parent,
                                   std::unique_ptr<Widget> child,
                                   views::View* host) {
    CHECK(parent != nullptr && parent->impl_ != nullptr);
    CHECK(child != nullptr);
    CHECK(host != nullptr);
    CHECK(child->impl_ != nullptr && child->impl_->native != nullptr);
    CHECK(child->impl_->native_host == nullptr);

    child->impl_->native_host = host;
    parent->impl_->children.push_back(std::move(child));
}

SkColor ToSkColor(const Color& color) {
    return SkColorSetARGB(color.a, color.r, color.g, color.b);
}

gfx::FontList DeriveFontList(const gfx::FontList& base,
                             const style::Text& text) {
    int font_style = base.GetFontStyle();
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

    const int size_delta = text.size ? *text.size - base.GetFontSize() : 0;
    gfx::Font::Weight weight = base.GetFontWeight();
    if (text.bold)
        weight = gfx::Font::Weight::BOLD;
    if (text.weight)
        weight = static_cast<gfx::Font::Weight>(*text.weight);

    return base.Derive(size_delta, font_style, weight);
}

}  // namespace internal

}  // namespace liew
