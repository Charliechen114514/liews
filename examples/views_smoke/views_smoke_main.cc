// views-standalone 冒烟 — 照抄 views/examples/examples_main_proc.cc 的初始化序列
#include <memory>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/i18n/icu_util.h"
#include "base/lazy_instance.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/test/allow_check_is_test_for_testing.h"
#include "base/test/task_environment.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "base/test/test_timeouts.h"
#include "build/build_config.h"
#include "components/viz/host/host_frame_sink_manager.h"
#include "components/viz/service/frame_sinks/frame_sink_manager_impl.h"
#include "mojo/core/embedder/embedder.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/ime/init/input_method_initializer.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/compositor/compositor_switches.h"
#include "ui/compositor/test/test_context_factories.h"
#include "ui/display/screen.h"
#include "ui/gfx/font_util.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/desktop_test_views_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/wm_state.h"

#if defined(USE_AURA)
#    include "ui/aura/env.h"
#endif
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
#    include "ui/views/widget/desktop_aura/desktop_screen.h"
#endif
#if BUILDFLAG(IS_WIN)
#    include "base/feature_list.h"
#    include "ui/accessibility/platform/ax_platform_for_test.h"
#    include "ui/base/win/scoped_ole_initializer.h"
#endif

namespace {
class SmokeDelegate : public views::WidgetDelegate {
  public:
    SmokeDelegate() {
        SetContentsView(std::make_unique<views::Label>(u"views-standalone smoke OK"));
    }

    std::u16string GetWindowTitle() const override { return u"views-standalone smoke"; }
    bool CanResize() const override { return true; }
};
} // namespace

#ifdef CreateWindow
#    undef CreateWindow
#endif

base::LazyInstance<base::TestDiscardableMemoryAllocator>::DestructorAtExit
    g_discardable_memory_allocator = LAZY_INSTANCE_INITIALIZER;

int main(int argc, char** argv) {
    base::AtExitManager at_exit;
    base::test::AllowCheckIsTestForTesting();
    base::CommandLine::Init(argc, argv);
    auto* cmd = base::CommandLine::ForCurrentProcess();
    cmd->AppendSwitch(switches::kDisableDirectComposition);
    cmd->AppendSwitchASCII("use-angle", "d3d11");
    // TaskEnvironment(真时间) 会装 10s RunLoop 超时 (TestTimeouts 默认) —
    // 窗口式冒烟要长存活, 自注 1 天上限 (TestTimeouts::Initialize 读此 switch)。
    cmd->AppendSwitchASCII("ui-test-action-timeout", "86400000");

#if BUILDFLAG(IS_WIN)
    ui::ScopedOleInitializer ole_initializer;
    ui::AXPlatformForTest ax_platform;
    base::FeatureList::InitInstance(cmd->GetSwitchValueASCII("enable-features"),
                                    cmd->GetSwitchValueASCII("disable-features"));
#endif

    mojo::core::Init();
    gl::init::InitializeGLOneOff(gl::GpuPreference::kDefault);
    base::i18n::InitializeICU();
    ui::RegisterPathProvider();
    base::DiscardableMemoryAllocator::SetInstance(g_discardable_memory_allocator.Pointer());
    gfx::InitializeFonts();
    TestTimeouts::Initialize();
    base::test::TaskEnvironment task_environment(base::test::TaskEnvironment::MainThreadType::UI);

    auto context_factories = std::make_unique<ui::TestContextFactories>(
        /*enable_pixel_output=*/false, /*output_to_window=*/true);

    base::FilePath pak;
    CHECK(base::PathService::Get(ui::UI_TEST_PAK, &pak));
    ui::ResourceBundle::InitSharedInstanceWithPakPath(pak);

#if defined(USE_AURA)
    std::unique_ptr<aura::Env> env = aura::Env::CreateInstance();
    env->set_context_factory(context_factories->GetContextFactory());
#endif
    ui::InitializeInputMethodForTesting();

    views::DesktopTestViewsDelegate views_delegate;
#if defined(USE_AURA)
    wm::WMState wm_state;
#endif
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
    std::unique_ptr<display::Screen> desktop_screen = views::CreateDesktopScreen();
#endif

    // Current Views ownership model: the client owns both objects, the delegate
    // outlives the widget, and the contents View is transferred independently.
    auto delegate = std::make_unique<SmokeDelegate>();
    auto widget = std::make_unique<views::Widget>();
    views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    params.delegate = delegate.get();
    params.bounds = gfx::Rect(50, 50, 480, 300);
    widget->Init(std::move(params));

    base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
    widget->MakeCloseSynchronous(base::BindOnce(
        [](std::unique_ptr<views::Widget>* widget, base::RunLoop* run_loop,
           views::Widget::ClosedReason) {
            widget->reset();
            run_loop->QuitWhenIdle();
        },
        &widget, &run_loop));
    widget->Show();
    run_loop.Run();

    ui::ResourceBundle::CleanupSharedInstance();
    ui::ShutdownInputMethod();
#if defined(USE_AURA)
    env.reset();
#endif
    ui::Clipboard::DestroyClipboardForCurrentThread();
    return 0;
}
