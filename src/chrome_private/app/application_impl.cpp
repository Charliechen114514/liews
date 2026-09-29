#include "src/chrome_private/app/application_impl.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"

namespace liew {

Application::Impl::Impl(AppConfig config)
    : application_name_(config.application_name ? config.application_name : u"liew"),
      process_(internal::ProcessRuntime::Options{
          config.argument_count_, config.argument_params_}),
      accessibility_platform_(std::make_unique<internal::AccessibilityPlatform>(
          base::UTF16ToUTF8(application_name_))),
      compositor_runtime_(std::make_unique<CompositorRuntime>()),
      resource_bundle_(internal::ScopedResourceBundle::Options{
          .pak_name = config.resource_pak_name}),
      aura_env_(compositor_runtime_->context_factory()),
      views_delegate_(std::make_unique<internal::LiewViewsDelegate>(
          base::UTF16ToUTF8(application_name_))),
      window_manager_(std::make_unique<internal::WindowManager>(
          base::BindRepeating(&Impl::QuitIfAllWindowsClosed, base::Unretained(this)))) {}

Application::Impl::~Impl() = default;

int Application::Impl::Run() {
    CHECK(!run_loop_) << "Application::Run() is not reentrant";
    if (window_manager_->empty()) {
        return 0;
    }

    base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
    run_loop_ = &run_loop;
    run_loop.Run();
    run_loop_ = nullptr;
    return 0;
}

void Application::Impl::Quit() {
    if (run_loop_) {
        run_loop_->QuitWhenIdle();
    }
}

bool Application::Impl::IsRunning() const {
    return run_loop_ != nullptr;
}

void Application::Impl::QuitIfAllWindowsClosed() {
    if (run_loop_ && window_manager_->empty()) {
        // CLIENT_OWNS_WIDGET tears down Widget synchronously, but its desktop
        // native widget posts the final HWND/compositor close. Keep pumping
        // until those already-queued tasks finish so the compositor runtime
        // sees no live compositors during shutdown.
        run_loop_->QuitWhenIdle();
    }
}

}  // namespace liew
