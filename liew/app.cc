// liew::Application — 实现。初始化/收尾序列搬运自 views_smoke 冒烟
// (examples/views_smoke/views_smoke_main.cc), RAII 化为 Impl 成员:
// 成员声明顺序 = 构造顺序, 析构逆序收尾, 与冒烟 main 的栈序严格一致。
// 冒烟口径: 构造 = 启动序列 + 开一个 800x600 居中窗口 + 跑消息循环到窗口
// 关闭; 后续 API 演进 (Run/NewWindow/事件分发...) 由项目作者接管。
#include "liew/liew_app.h"

#include <map>
#include <set>

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
#include "mojo/core/embedder/embedder.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/ime/init/input_method_initializer.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/color/color_id.h"
#include "ui/compositor/compositor_switches.h"
#include "ui/compositor/test/test_context_factories.h"
#include "ui/display/screen.h"
#include "ui/gfx/font_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/views/background.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/test/desktop_test_views_delegate.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
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

#ifdef CreateWindow
#    undef CreateWindow
#endif

namespace liew {
namespace {

// 冒烟同款: 进程级 discardable 内存分配器 (DestructorAtExit 挂 AtExitManager)。
base::LazyInstance<base::TestDiscardableMemoryAllocator>::DestructorAtExit
    g_discardable_memory_allocator = LAZY_INSTANCE_INITIALIZER;

// 窗口内容视图: FillLayout + 主题色实心背景。自定义内容往
// widget->GetContentsView() 里 AddChildView 即可铺满客户区。
class ContentView : public views::View {
  public:
    ContentView() {
        SetLayoutManager(std::make_unique<views::FillLayout>());
        SetBackground(views::CreateSolidBackground(ui::kColorWindowBackground));
    }
};

// 每窗一个 WidgetDelegate。内容视图以 unique_ptr 显式移交给 View 树；
// delegate 与 Widget 则由 Application 分别持有。
class WindowDelegate : public views::WidgetDelegate {
  public:
    WindowDelegate(std::u16string title, bool resizable)
        : title_(std::move(title)), resizable_(resizable) {
        SetContentsView(std::make_unique<ContentView>());
    }

    // views::WidgetDelegate:
    std::u16string GetWindowTitle() const override { return title_; }
    bool CanResize() const override { return resizable_; }

  private:
    std::u16string title_;
    bool resizable_;
};

// 声明顺序是所有权约束：析构逆序，Widget 必须先于其 delegate 销毁。
struct WindowState {
    std::unique_ptr<WindowDelegate> delegate;
    std::unique_ptr<views::Widget> widget;
};

} // namespace

class Application::Impl : public views::WidgetObserver {
  public:
    explicit Impl(const AppConfig& config) {
        (void)config; // AppConfig 暂为空壳, 预留。
        base::test::AllowCheckIsTestForTesting();
        base::CommandLine::Init(0, nullptr);
        auto* cmd = base::CommandLine::ForCurrentProcess();
        cmd->AppendSwitch(switches::kDisableDirectComposition);
        cmd->AppendSwitchASCII("use-angle", "d3d11");
        // TaskEnvironment(真时间) 会装 10s RunLoop 超时 (TestTimeouts 默认) —
        // 窗口式应用要长存活, 自注 1 天上限 (TestTimeouts::Initialize 读此 switch)。
        cmd->AppendSwitchASCII("ui-test-action-timeout", "86400000");

#if BUILDFLAG(IS_WIN)
        ole_ = std::make_unique<ui::ScopedOleInitializer>();
        ax_platform_ = std::make_unique<ui::AXPlatformForTest>();
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
        task_environment_ = std::make_unique<base::test::TaskEnvironment>(
            base::test::TaskEnvironment::MainThreadType::UI);
        context_factories_ = std::make_unique<ui::TestContextFactories>(
            /*enable_pixel_output=*/false, /*output_to_window=*/true);

        base::FilePath pak;
        CHECK(base::PathService::Get(ui::UI_TEST_PAK, &pak));
        ui::ResourceBundle::InitSharedInstanceWithPakPath(pak);

#if defined(USE_AURA)
        env_ = aura::Env::CreateInstance();
        env_->set_context_factory(context_factories_->GetContextFactory());
#endif
        ui::InitializeInputMethodForTesting();

        views_delegate_ = std::make_unique<views::DesktopTestViewsDelegate>();
#if defined(USE_AURA)
        wm_state_ = std::make_unique<wm::WMState>();
#endif
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
        desktop_screen_ = views::CreateDesktopScreen();
#endif

        // 冒烟判据: 一个 800x600 居中、可缩放、默认标题的窗口。
        NewWindow(u"liew window", gfx::Size(800, 600), /*resizable=*/true,
                  /*center=*/true);
        RunUntilAllWindowsClosed();
    }

    ~Impl() override {
        // CLIENT_OWNS_WIDGET：先摘观察者，再释放 Widget；WindowState 保证其
        // delegate 在 Widget 析构完成后才销毁。
        for (auto& entry : windows_) {
            entry.first->RemoveObserver(this);
        }
        windows_.clear();
        alive_widgets_.clear();
        ui::ResourceBundle::CleanupSharedInstance();
        ui::ShutdownInputMethod();
#if defined(USE_AURA)
        env_.reset();
#endif
        ui::Clipboard::DestroyClipboardForCurrentThread();
    }

  private:
    views::Widget* NewWindow(const std::u16string& title, const gfx::Size& size, bool resizable,
                             bool center) {
        auto state = std::make_unique<WindowState>();
        state->delegate = std::make_unique<WindowDelegate>(title, resizable);
        state->widget = std::make_unique<views::Widget>();

        views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
        params.delegate = state->delegate.get();
        params.bounds = gfx::Rect(size);
        state->widget->Init(std::move(params));

        views::Widget* widget = state->widget.get();
        if (center) {
            widget->CenterWindow(size);
        }
        widget->AddObserver(this);
        widget->MakeCloseSynchronous(
            base::BindOnce(&Impl::CloseWindow, base::Unretained(this), widget));
        windows_.emplace(widget, std::move(state));
        alive_widgets_.insert(widget);
        widget->Show();
        return widget;
    }

    void CloseWindow(views::Widget* widget, views::Widget::ClosedReason) {
        auto it = windows_.find(widget);
        if (it == windows_.end()) {
            return;
        }

        widget->RemoveObserver(this);
        alive_widgets_.erase(widget);
        auto state = std::move(it->second);
        windows_.erase(it);
        state.reset();
        QuitIfAllWindowsClosed();
    }

    void QuitIfAllWindowsClosed() {
        if (run_loop_ && alive_widgets_.empty()) {
            // CLIENT_OWNS_WIDGET tears down Widget synchronously, but its desktop
            // native widget posts the final HWND/compositor close. Keep pumping until
            // those already-queued tasks finish so TestContextFactories sees no
            // live compositors during shutdown.
            run_loop_->QuitWhenIdle();
        }
    }

    void RunUntilAllWindowsClosed() {
        if (alive_widgets_.empty()) {
            return;
        }
        base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
        run_loop_ = &run_loop;
        run_loop.Run();
        run_loop_ = nullptr;
    }

    // views::WidgetObserver:
    void OnWidgetDestroyed(views::Widget* widget) override {
        alive_widgets_.erase(widget);
        QuitIfAllWindowsClosed();
    }

    // --- 启动序列成员 (构造顺序 = 声明顺序, 析构逆序收尾 = 冒烟 main 栈序) ---
    base::AtExitManager at_exit_; // 必须先于一切 LazyInstance/Singleton
#if BUILDFLAG(IS_WIN)
    std::unique_ptr<ui::ScopedOleInitializer> ole_;
    std::unique_ptr<ui::AXPlatformForTest> ax_platform_;
#endif
    std::unique_ptr<base::test::TaskEnvironment> task_environment_;
    std::unique_ptr<ui::TestContextFactories> context_factories_;
#if defined(USE_AURA)
    std::unique_ptr<aura::Env> env_;
#endif
    std::unique_ptr<views::DesktopTestViewsDelegate> views_delegate_;
#if defined(USE_AURA)
    std::unique_ptr<wm::WMState> wm_state_;
#endif
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
    std::unique_ptr<display::Screen> desktop_screen_;
#endif

    // Run() 期间非空; 归零即 Quit。
    base::RunLoop* run_loop_ = nullptr;
    std::map<views::Widget*, std::unique_ptr<WindowState>> windows_;
    std::set<views::Widget*> alive_widgets_;
};

Application::Application(AppConfig config) : impl_(new Impl(config)) {}

Application::~Application() {
    delete impl_;
}

} // namespace liew
