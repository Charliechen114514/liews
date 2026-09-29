#pragma once

#include <functional>
#include <vector>

#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

struct MenuItem {
    const char16_t* text = nullptr;  // null = 分隔线
    std::function<void()> on_click;
    bool enabled = true;
    bool checked = false;
};

class LIEW_API Menu {
  public:
    Menu() = delete;

    // 一次性菜单:在 anchor 控件 (x,y)(相对其左上角)处弹出,
    // 关闭后自毁,调用方无需(也不能)持有任何对象。
    static void Show(const std::vector<MenuItem>& items,
                     Widget* anchor,
                     int x,
                     int y);

  private:
    class Impl;
};

}  // namespace liew
