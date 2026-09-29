#pragma once

#include <functional>

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

struct TextFieldCallback {
    std::function<void(const char16_t* text)> on_changed;
};

class LIEW_API TextField : public Widget {
  public:
    TextField();
    ~TextField() override;

    void SetText(const char16_t* text);
    const char16_t* GetText() const;
    void SetPlaceholder(const char16_t* text);

    void SetCallback(const TextFieldCallback& callback);

  private:
    class Impl;
};

}  // namespace liew
