#pragma once

#include <functional>

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"
#include "liew/widget/widgets/button_style.h"

namespace liew {

struct ButtonCallback {
    std::function<void()> on_click;
};

class LIEW_API Button : public Widget {
  public:
    Button(const char16_t* text = u"");
    ~Button() override;

    void SetText(const char16_t* text);
    const char16_t* GetText() const;

    void SetCallback(const ButtonCallback& callback);

    void SetStyle(ButtonStyle style);

  private:
    class Impl;
};

} // namespace liew
