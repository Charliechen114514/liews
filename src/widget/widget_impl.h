#pragma once

#include <memory>
#include <vector>

#include "liew/base/gui/color.h"
#include "liew/base/gui/style/text.h"
#include "liew/widget/basic/widget.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/font_list.h"
#include "ui/views/view.h"

namespace liew {

class Widget::Impl {
  public:
    virtual ~Impl() = default;

    // native 删除前回调:释放指向 native 内部的反向引用(订阅等),防 UAF。
    virtual void WillDestroyNative() {}

    views::View* native = nullptr;
    views::View* native_host = nullptr;
    std::vector<std::unique_ptr<Widget>> children;
};

namespace internal {

class WidgetBridge {
  public:
    static void MountToNativeHost(Widget* widget, views::View* host_native);
    static views::View* GetNative(const Widget* widget);

    // 外部托管路径:native 的父子关系由 views 侧 API 建立时使用。
    static views::View* TakeNativeOwnership(Widget* widget);
    static void AdoptChildShell(Widget* parent,
                                std::unique_ptr<Widget> child,
                                views::View* host);
};

SkColor ToSkColor(const Color& color);
gfx::FontList DeriveFontList(const gfx::FontList& base,
                             const style::Text& text);

}  // namespace internal

}  // namespace liew
