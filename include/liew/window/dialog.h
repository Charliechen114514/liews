#pragma once

#include <functional>

#include "liew/app/liew_app_config.h"
#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

struct DialogConfig {
    const char16_t* title = nullptr;
    Size size{360, 220};
    bool modal = true;
};

class LIEW_API Dialog {
  public:
    Dialog() = delete;

    // 一次性对话框:fill 在显示前布置 RootView(默认 FillLayout);
    // 对话框关闭后自毁,调用方无需(也不能)持有任何对象。
    // anchor 控件所在的窗口成为模态父。
    static void Show(Widget* anchor,
                     DialogConfig config,
                     std::function<void(Widget* root)> fill);

    Dialog(const Dialog&) = delete;
    Dialog& operator=(const Dialog&) = delete;

  private:
    class Impl;
};

}  // namespace liew
