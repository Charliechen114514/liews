#pragma once

#include <memory>

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

class LIEW_API TabbedPane : public Widget {
  public:
    TabbedPane();
    ~TabbedPane() override;

    template <typename T>
    T* AddTab(const char16_t* title, std::unique_ptr<T> page) {
        return static_cast<T*>(AddTabImpl(title, std::move(page)));
    }

    int GetSelectedIndex() const;
    void SelectTabAt(int index);

  private:
    Widget* AddTabImpl(const char16_t* title, std::unique_ptr<Widget> page);

    class Impl;
};

}  // namespace liew
