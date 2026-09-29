#pragma once

#include "liew/base/gui/style/text.h"
#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

struct LabelStyle {
    style::Text text_style;
};

class LIEW_API Label : public Widget {
  public:
    explicit Label(const char16_t* text = u"");
    ~Label() override;

    void SetText(const char16_t* text);
    const char16_t* GetText() const;

    void SetStyle(LabelStyle style);

  private:
    class Impl;
};

}  // namespace liew
