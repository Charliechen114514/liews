#pragma once

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

class LIEW_API ProgressBar : public Widget {
  public:
    ProgressBar();
    ~ProgressBar() override;

    double GetValue() const;
    void SetValue(double value);

  private:
    class Impl;
};

}  // namespace liew
