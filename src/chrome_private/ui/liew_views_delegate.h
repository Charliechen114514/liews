#pragma once
// LiewViewsDelegate — liew 框架派驻 views 框架的发言人(适配器)。
// 回答 views 的应用级问题:应用身份 + native widget 后端分诊。
// 树重依赖(native_widget_aura 等)全部关在 .cpp,头文件保持轻量可 include。
//
// 战略储备:OnBeforeWidgetInit 是窗口后端策略的入口 —— 未来 AppConfig 若
// 增加窗口策略选项(无边框/软件渲染等),都在此分诊处生长。

#include <string>

#include "ui/views/layout/layout_provider.h"
#include "ui/views/views_delegate.h"

namespace liew::internal {

class LiewViewsDelegate final : public views::ViewsDelegate {
  public:
    explicit LiewViewsDelegate(std::string application_name);

    // views::ViewsDelegate:
    std::string GetApplicationName() override;
    void OnBeforeWidgetInit(views::Widget::InitParams* params,
                            views::internal::NativeWidgetDelegate* delegate) override;

  private:
    std::string application_name_;
    views::LayoutProvider layout_provider_;
};

}  // namespace liew::internal
