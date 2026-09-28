#include "liew/chrome_private/ui/ui_toolkit.h"

#include <utility>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/ime/init/input_method_initializer.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/compositor/compositor.h"
#include "ui/display/screen.h"
#include "ui/views/buildflags.h"
#include "ui/views/views_delegate.h"

#if defined(USE_AURA)
#    include "ui/aura/env.h"
#    include "ui/wm/core/wm_state.h"
#endif
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
#    include "ui/views/widget/desktop_aura/desktop_screen.h"
#endif

namespace liew::internal {

// --- ScopedResourceBundle ---

ScopedResourceBundle::ScopedResourceBundle(const Options& options) {
    base::FilePath pak_path = options.pak_path;
    if (pak_path.empty()) {
        CHECK(base::PathService::Get(base::DIR_ASSETS, &pak_path));
        pak_path = pak_path.AppendASCII(options.pak_name);
    }
    ui::ResourceBundle::InitSharedInstanceWithPakPath(pak_path);
}

ScopedResourceBundle::~ScopedResourceBundle() {
    ui::ResourceBundle::CleanupSharedInstance();
}

// --- ScopedAuraEnv ---

ScopedAuraEnv::ScopedAuraEnv(ui::ContextFactory* context_factory) {
#if defined(USE_AURA)
    env_ = aura::Env::CreateInstance();
    env_->set_context_factory(context_factory);
#endif
}

ScopedAuraEnv::~ScopedAuraEnv() {
#if defined(USE_AURA)
    env_.reset();                      // Env 复位
    DCHECK(!aura::Env::HasInstance()); // 自检:装配层拆卸干净
#endif
}

// --- ScopedInputMethod ---

ScopedInputMethod::ScopedInputMethod() {
    ui::InitializeInputMethod();
}

ScopedInputMethod::~ScopedInputMethod() {
    ui::ShutdownInputMethod();
}

// --- ScopedViewsDelegate ---

ScopedViewsDelegate::ScopedViewsDelegate(std::unique_ptr<views::ViewsDelegate> delegate)
    : delegate_(std::move(delegate)) {
    // ViewsDelegate 注册/卸任发生在 delegate_ 的构造/析构内(上游契约)。
    CHECK(delegate_);
}

ScopedViewsDelegate::~ScopedViewsDelegate() = default;

// --- ScopedWMState ---

ScopedWMState::ScopedWMState() {
#if defined(USE_AURA)
    wm_state_ = std::make_unique<wm::WMState>();
#endif
}

ScopedWMState::~ScopedWMState() = default;

// --- ScopedDesktopScreen ---

ScopedDesktopScreen::ScopedDesktopScreen() {
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
    screen_ = views::CreateDesktopScreen();
#endif
}

ScopedDesktopScreen::~ScopedDesktopScreen() = default;

// --- ScopedClipboard ---

ScopedClipboard::~ScopedClipboard() {
    ui::Clipboard::DestroyClipboardForCurrentThread();
}

} // namespace liew::internal
