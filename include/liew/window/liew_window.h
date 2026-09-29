#pragma once
// liew::Window — 框架窗口(公共 API)。
//
// 生命周期语义:
//   * 窗口对象析构 = 关闭该窗(若尚未被用户关闭);
//   * 最后一个窗口消失(无论 UI 关闭还是析构)→ 应用退出。
// 这是框架结构语义,不是可配置策略 —— 窗口即应用的存在理由。
//
// 约束:Window 必须先于 Application 析构(栈上先声明 app、后声明 window
// 的自然顺序即满足)。

#include "liew/app/liew_app_config.h"
#include "liew/base/class_macro.h"
#include "liew/liew_export.h"

namespace liew {

class Application;
class Widget;

class LIEW_API Window {
  public:
    // 创建并立即显示顶层窗口。title 为空时使用 AppConfig::application_name。
    explicit Window(Application& application, WindowConfig config = {});
    ~Window();

    // 请求关闭;若窗口已被用户关闭则为空操作。
    void Close();

    // 窗口内容根容器(非拥有);窗口已关闭时返回 nullptr。
    Widget* RootView();

  private:
    LIEW_DISABLE_COPY_MOVE(Window);

    class Impl;
    Impl* impl_;
};

}  // namespace liew
