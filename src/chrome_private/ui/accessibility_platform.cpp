#include "src/chrome_private/ui/accessibility_platform.h"

#include <utility>

namespace liew::internal {

AccessibilityPlatform::AccessibilityPlatform(std::string application_name)
    : application_name_(std::move(application_name)), platform_(*this) {}

ui::AXMode AccessibilityPlatform::GetAccessibilityMode() {
    return mode_;
}

#if BUILDFLAG(IS_WIN)
ui::AXPlatform::ProductStrings AccessibilityPlatform::GetProductStrings() {
    return {application_name_, "1.0", "liew Views"};
}
#endif

void AccessibilityPlatform::OnMinimalPropertiesUsed() {
    Enable(ui::AXMode::kNativeAPIs);
}

void AccessibilityPlatform::OnPropertiesUsedInBrowserUI() {
    Enable(ui::AXMode::kNativeAPIs);
}

void AccessibilityPlatform::OnPropertiesUsedInWebContent() {
    Enable(ui::AXMode::kNativeAPIs | ui::AXMode::kWebContents);
}

void AccessibilityPlatform::OnInlineTextBoxesUsedInWebContent() {
    Enable(ui::AXMode::kNativeAPIs | ui::AXMode::kWebContents | ui::AXMode::kInlineTextBoxes);
}

void AccessibilityPlatform::OnExtendedPropertiesUsedInWebContent() {
    Enable(ui::AXMode::kNativeAPIs | ui::AXMode::kWebContents |
           ui::AXMode::kExtendedProperties);
}

void AccessibilityPlatform::OnHTMLAttributesUsed() {
    Enable(ui::AXMode::kWebContents | ui::AXMode::kHTML);
}

void AccessibilityPlatform::OnActionFromAssistiveTech() {
    Enable(ui::AXMode::kNativeAPIs);
}

void AccessibilityPlatform::Enable(uint32_t flags) {
    const ui::AXMode added(flags & ~mode_.flags());
    if (added.is_mode_off()) {
        return;
    }
    mode_ |= added;
    platform_.NotifyModeAdded(added);
}

}  // namespace liew::internal
