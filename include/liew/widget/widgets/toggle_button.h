#pragma once

#include <functional>

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

struct ToggleButtonCallback {
    std::function<void(bool on)> on_toggled;
};

class LIEW_API ToggleButton : public Widget {
  public:
    ToggleButton();
    ~ToggleButton() override;

    bool IsOn() const;
    void SetOn(bool on);

    void SetCallback(const ToggleButtonCallback& callback);

  private:
    class Impl;
};

}  // namespace liew
