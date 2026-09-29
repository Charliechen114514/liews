#include <functional>
#include <string>
#include <utility>

#include "liew/widget/widgets/slider.h"
#include "src/widget/widget_impl.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/slider.h"

namespace liew {
namespace {

class ListenerBridge final : public views::SliderListener {
  public:
    std::function<void(float)> on_value_changed;

    // views::SliderListener:
    void SliderValueChanged(views::Slider* sender,
                            float value,
                            float old_value,
                            views::SliderChangeReason reason) override {
        if (on_value_changed)
            on_value_changed(value);
    }
};

}  // namespace

class Slider::Impl : public Widget::Impl {
  public:
    views::Slider* slider() { return static_cast<views::Slider*>(native); }

    ListenerBridge listener;
};

Slider::Slider() {
    impl_ = new Slider::Impl;
    auto* impl = static_cast<Slider::Impl*>(impl_);
    impl->native = new views::Slider(&impl->listener);

    impl->slider()->GetViewAccessibility().SetName(
        std::u16string(), ax::mojom::NameFrom::kAttributeExplicitlyEmpty);
}

Slider::~Slider() = default;

float Slider::GetValue() const {
    return static_cast<Slider::Impl*>(impl_)->slider()->GetValue();
}

void Slider::SetValue(float value) {
    static_cast<Slider::Impl*>(impl_)->slider()->SetValue(value);
}

void Slider::SetCallback(const SliderCallback& callback) {
    static_cast<Slider::Impl*>(impl_)->listener.on_value_changed =
        callback.on_value_changed;
}

}  // namespace liew
