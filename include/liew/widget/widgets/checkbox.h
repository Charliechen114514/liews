#pragma once

#include <functional>

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

struct CheckboxCallback {
    std::function<void(bool checked)> on_checked_changed;
};

class LIEW_API Checkbox : public Widget {
  public:
    explicit Checkbox(const char16_t* text = u"");
    ~Checkbox() override;

    bool IsChecked() const;
    void SetChecked(bool checked);

    void SetCallback(const CheckboxCallback& callback);

  private:
    class Impl;
};

}  // namespace liew
