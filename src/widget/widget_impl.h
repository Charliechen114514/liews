#pragma once

#include <memory>
#include <vector>

#include "liew/widget/basic/widget.h"
#include "ui/views/view.h"

namespace liew {

class Widget::Impl {
  public:
    virtual ~Impl() = default;

    views::View* native = nullptr;
    views::View* native_host = nullptr;
    std::vector<std::unique_ptr<Widget>> children;
};

namespace internal {

class WidgetBridge {
  public:
    static void MountToNativeHost(Widget* widget, views::View* host_native);
    static views::View* GetNative(const Widget* widget);
};

}  // namespace internal

}  // namespace liew
