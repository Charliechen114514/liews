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
