#pragma once

#include <functional>

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

struct SliderCallback {
    std::function<void(float value)> on_value_changed;
};

class LIEW_API Slider : public Widget {
  public:
    Slider();
    ~Slider() override;

    float GetValue() const;
    void SetValue(float value);

    void SetCallback(const SliderCallback& callback);

  private:
    class Impl;
};

}  // namespace liew
