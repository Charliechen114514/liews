#pragma once

#include <cstddef>
#include <memory>

#include "liew/liew_export.h"
#include "liew/widget/layout/box_layout.h"
#include "liew/widget/layout/filled_layout.h"

namespace liew {

namespace internal {
class WidgetBridge;
}

class LIEW_API Widget {
  public:
    virtual ~Widget();
    template <typename T>
    T* AddChild(std::unique_ptr<T> child) {
        T* raw = child.get();
        AddChildImpl(std::move(child));
        return raw;
    }

    void ClearChildren();

    // 空指针/空串 = 显式声明"永无障碍名"(可聚焦控件的读屏检查合规)。
    void SetAccessibleName(const char16_t* name);

    // 关闭本控件所在的顶层窗口(对话框/主窗皆适用)。
    void CloseWindow();

    void SetLayout(FilledLayout layout);
    void SetLayout(BoxLayout layout);

    static void* operator new(std::size_t size);
    static void operator delete(void* ptr) noexcept;

  protected:
    Widget();

    class Impl;
    Impl* impl_;

  private:
    friend class internal::WidgetBridge;

    void AddChildImpl(std::unique_ptr<Widget> child);
};

} // namespace liew
