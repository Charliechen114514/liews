#pragma once

#include <functional>
#include <string>
#include <vector>

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

struct ComboboxCallback {
    std::function<void(int index)> on_selected_changed;
};

class LIEW_API Combobox : public Widget {
  public:
    explicit Combobox(const std::vector<std::u16string>& items);
    ~Combobox() override;

    int GetSelectedIndex() const;
    void SetSelectedIndex(int index);

    void SetCallback(const ComboboxCallback& callback);

  private:
    class Impl;
};

}  // namespace liew
