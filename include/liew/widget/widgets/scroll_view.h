#pragma once

#include <memory>

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

class LIEW_API ScrollView : public Widget {
  public:
    ScrollView();
    ~ScrollView() override;

    // 独占内容,再次调用会替换(旧内容整树销毁)。
    template <typename T>
    T* SetContent(std::unique_ptr<T> content) {
        return static_cast<T*>(SetContentImpl(std::move(content)));
    }

  private:
    Widget* SetContentImpl(std::unique_ptr<Widget> content);

    class Impl;
};

}  // namespace liew
