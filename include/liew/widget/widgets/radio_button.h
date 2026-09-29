#pragma once

#include "liew/liew_export.h"
#include "liew/widget/widgets/checkbox.h"

namespace liew {

class LIEW_API RadioButton : public Widget {
  public:
    explicit RadioButton(const char16_t* text = u"");
    ~RadioButton() override;

    bool IsChecked() const;
    void SetChecked(bool checked);

    void SetCallback(const CheckboxCallback& callback);

  private:
    class Impl;
};

}  // namespace liew
