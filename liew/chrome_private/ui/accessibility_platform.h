#pragma once
// AccessibilityPlatform — ui::AXPlatform::Delegate 的独立嵌入者实现。
// AXMode 升档策略:收到任何使用信号(读屏软件查询)时按需打开对应模式位,
// 并经 ui::AXPlatform 通知模式变化。

#include <cstdint>
#include <string>

#include "build/build_config.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/platform/ax_platform.h"

namespace liew::internal {

class AccessibilityPlatform final : public ui::AXPlatform::Delegate {
  public:
    explicit AccessibilityPlatform(std::string application_name);

    // ui::AXPlatform::Delegate:
    ui::AXMode GetAccessibilityMode() override;

#if BUILDFLAG(IS_WIN)
    ui::AXPlatform::ProductStrings GetProductStrings() override;
#endif

    void OnMinimalPropertiesUsed() override;
    void OnPropertiesUsedInBrowserUI() override;
    void OnPropertiesUsedInWebContent() override;
    void OnInlineTextBoxesUsedInWebContent() override;
    void OnExtendedPropertiesUsedInWebContent() override;
    void OnHTMLAttributesUsed() override;
    void OnActionFromAssistiveTech() override;

  private:
    void Enable(uint32_t flags);

    std::string application_name_;
    ui::AXMode mode_;
    ui::AXPlatform platform_;  // 以 *this 为 delegate;声明在最后,析构先撤销注册
};

}  // namespace liew::internal
