#include "liew/chrome_private/ui/liew_views_delegate.h"

#include <utility>

#include "build/build_config.h"
#include "ui/views/widget/native_widget_aura.h"

#if BUILDFLAG(ENABLE_DESKTOP_AURA)
#    include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#endif

namespace liew::internal {

LiewViewsDelegate::LiewViewsDelegate(std::string application_name)
    : application_name_(std::move(application_name)) {}

std::string LiewViewsDelegate::GetApplicationName() {
    return application_name_;
}

void LiewViewsDelegate::OnBeforeWidgetInit(views::Widget::InitParams* params,
                                           views::internal::NativeWidgetDelegate* delegate) {
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
    if (params->native_widget) {
        return;
    }
    if (params->parent && params->type != views::Widget::InitParams::TYPE_MENU &&
        params->type != views::Widget::InitParams::TYPE_TOOLTIP) {
        params->native_widget = new views::NativeWidgetAura(delegate);
    } else {
        params->native_widget = new views::DesktopNativeWidgetAura(delegate);
    }
#endif
}

} // namespace liew::internal
