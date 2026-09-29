#include "src/widget/widget_impl.h"

#include "base/check.h"
#include "liew/widget/widgets/container.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"

namespace liew {

Widget::Widget() : impl_(nullptr) {}

Widget::~Widget() {
    impl_->children.clear();
    if (impl_->native != nullptr) {
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

}  // namespace internal

}  // namespace liew
