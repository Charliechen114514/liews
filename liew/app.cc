// liew::Application — process runtime and top-level window lifecycle.
// Construction initializes the runtime only. Window creation and message-loop
// execution are explicit public operations.
#include "liew/compositor_runtime.h"
#include "liew/liew_app.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/at_exit.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/i18n/icu_util.h"
#include "base/memory/discardable_memory.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/system/sys_info.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "build/build_config.h"
#include "mojo/core/embedder/embedder.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/ime/init/input_method_initializer.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/color/color_id.h"
#include "ui/display/screen.h"
#include "ui/gfx/font_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/background.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/wm_state.h"

#if defined(USE_AURA)
#    include "ui/aura/env.h"
#endif
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
#    include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#    include "ui/views/widget/desktop_aura/desktop_screen.h"
#endif
#if BUILDFLAG(IS_WIN)
#    include "base/feature_list.h"
#    include "ui/base/win/scoped_ole_initializer.h"
#endif

#ifdef CreateWindow
#    undef CreateWindow
#endif

namespace liew {
namespace {

constexpr char kResourcePakName[] = "liew_resources.pak";

// Standalone embedders have no browser-side discardable-memory Mojo service.
// This process-local allocator keeps the same lock contract while using heap
// storage; the process-wide instance intentionally has application lifetime.
struct DiscardableMemoryState {
    std::atomic_size_t bytes_allocated = 0;
};

class HeapDiscardableMemory final : public base::DiscardableMemory {
  public:
    HeapDiscardableMemory(DiscardableMemoryState* state, size_t size)
        : state_(state), size_(size), data_(std::make_unique<uint8_t[]>(size)) {
        state_->bytes_allocated.fetch_add(size_, std::memory_order_relaxed);
    }

    ~HeapDiscardableMemory() override { ReleaseStorage(); }

    bool Lock() override {
        DCHECK(!locked_);
        if (!data_) {
            return false;
        }
        locked_ = true;
        return true;
    }

    void Unlock() override {
        DCHECK(locked_);
        locked_ = false;
    }

    void* data() const override {
        DCHECK(locked_);
        return data_.get();
    }

    void DiscardForTesting() override {
        DCHECK(!locked_);
        ReleaseStorage();
    }

    base::trace_event::MemoryAllocatorDump*
    CreateMemoryAllocatorDump(const char*, base::trace_event::ProcessMemoryDump*) const override {
        return nullptr;
    }

  private:
    void ReleaseStorage() {
        if (!data_) {
            return;
        }
        data_.reset();
        state_->bytes_allocated.fetch_sub(size_, std::memory_order_relaxed);
    }

    DiscardableMemoryState* state_;
    const size_t size_;
    std::unique_ptr<uint8_t[]> data_;
    bool locked_ = true;
};

class HeapDiscardableMemoryAllocator final : public base::DiscardableMemoryAllocator {
  public:
    std::unique_ptr<base::DiscardableMemory> AllocateLockedDiscardableMemory(size_t size) override {
        return std::make_unique<HeapDiscardableMemory>(&state_, size);
    }

    size_t GetBytesAllocated() const override {
        return state_.bytes_allocated.load(std::memory_order_relaxed);
    }

    void ReleaseFreeMemory() override {}

  private:
    DiscardableMemoryState state_;
};

HeapDiscardableMemoryAllocator* GetDiscardableMemoryAllocator() {
    static base::NoDestructor<HeapDiscardableMemoryAllocator> allocator;
    return allocator.get();
}

class AccessibilityPlatform final : public ui::AXPlatform::Delegate {
  public:
    explicit AccessibilityPlatform(std::string application_name)
        : application_name_(std::move(application_name)), platform_(*this) {}

    ui::AXMode GetAccessibilityMode() override { return mode_; }

#if BUILDFLAG(IS_WIN)
    ui::AXPlatform::ProductStrings GetProductStrings() override {
        return {application_name_, "1.0", "liew Views"};
    }
#endif

    void OnMinimalPropertiesUsed() override { Enable(ui::AXMode::kNativeAPIs); }
    void OnPropertiesUsedInBrowserUI() override { Enable(ui::AXMode::kNativeAPIs); }
    void OnPropertiesUsedInWebContent() override {
        Enable(ui::AXMode::kNativeAPIs | ui::AXMode::kWebContents);
    }
    void OnInlineTextBoxesUsedInWebContent() override {
        Enable(ui::AXMode::kNativeAPIs | ui::AXMode::kWebContents | ui::AXMode::kInlineTextBoxes);
    }
    void OnExtendedPropertiesUsedInWebContent() override {
        Enable(ui::AXMode::kNativeAPIs | ui::AXMode::kWebContents |
               ui::AXMode::kExtendedProperties);
    }
    void OnHTMLAttributesUsed() override { Enable(ui::AXMode::kWebContents | ui::AXMode::kHTML); }
    void OnActionFromAssistiveTech() override { Enable(ui::AXMode::kNativeAPIs); }

  private:
    void Enable(uint32_t flags) {
        const ui::AXMode added(flags & ~mode_.flags());
        if (added.is_mode_off()) {
            return;
        }
        mode_ |= added;
        platform_.NotifyModeAdded(added);
    }

    std::string application_name_;
    ui::AXMode mode_;
    ui::AXPlatform platform_;
};

class LiewViewsDelegate final : public views::ViewsDelegate {
  public:
    explicit LiewViewsDelegate(std::string application_name)
        : application_name_(std::move(application_name)) {}

    std::string GetApplicationName() override { return application_name_; }

    void OnBeforeWidgetInit(views::Widget::InitParams* params,
                            views::internal::NativeWidgetDelegate* delegate) override {
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

  private:
    std::string application_name_;
};

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
    WindowId id = kInvalidWindowId;
    std::unique_ptr<WindowDelegate> delegate;
    std::unique_ptr<views::Widget> widget;
};

} // namespace

class Application::Impl {
  public:
    explicit Impl(AppConfig config)
        : application_name_(config.application_name ? config.application_name : u"liew") {
        if (!base::CommandLine::InitializedForCurrentProcess()) {
            base::CommandLine::Init(0, nullptr);
        }
        auto* cmd = base::CommandLine::ForCurrentProcess();

#if BUILDFLAG(IS_WIN)
        ole_ = std::make_unique<ui::ScopedOleInitializer>();
        if (!base::FeatureList::GetInstance()) {
            base::FeatureList::InitInstance(cmd->GetSwitchValueASCII("enable-features"),
                                            cmd->GetSwitchValueASCII("disable-features"));
        }
#endif

        mojo::core::Init();
        base::i18n::InitializeICU();
        if (!base::DiscardableMemoryAllocator::HasInstance()) {
            base::DiscardableMemoryAllocator::SetInstance(GetDiscardableMemoryAllocator());
        }
        gfx::InitializeFonts();
        task_executor_ =
            std::make_unique<base::SingleThreadTaskExecutor>(base::MessagePumpType::UI);
        if (!base::ThreadPoolInstance::Get()) {
            base::ThreadPoolInstance::Create("liew");
            base::ThreadPoolInstance::InitParams thread_pool_params(
                static_cast<size_t>(std::max(3, base::SysInfo::NumberOfProcessors() - 1)));
#if BUILDFLAG(IS_WIN)
            thread_pool_params.common_thread_pool_environment =
                base::ThreadPoolInstance::InitParams::CommonThreadPoolEnvironment::COM_MTA;
#endif
            base::ThreadPoolInstance::Get()->Start(thread_pool_params);
        }
        accessibility_platform_ =
            std::make_unique<AccessibilityPlatform>(base::UTF16ToUTF8(application_name_));
        compositor_runtime_ = std::make_unique<CompositorRuntime>();

        base::FilePath pak;
        CHECK(base::PathService::Get(base::DIR_ASSETS, &pak));
        ui::ResourceBundle::InitSharedInstanceWithPakPath(pak.AppendASCII(kResourcePakName));

#if defined(USE_AURA)
        env_ = aura::Env::CreateInstance();
        env_->set_context_factory(compositor_runtime_->context_factory());
#endif
        ui::InitializeInputMethod();

        views_delegate_ = std::make_unique<LiewViewsDelegate>(base::UTF16ToUTF8(application_name_));
#if defined(USE_AURA)
        wm_state_ = std::make_unique<wm::WMState>();
#endif
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
        desktop_screen_ = views::CreateDesktopScreen();
#endif
    }

    ~Impl() {
        // WindowState declares delegate before widget, so reverse destruction
        // tears down every Widget before its delegate.
        windows_.clear();
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
        desktop_screen_.reset();
#endif
#if defined(USE_AURA)
        wm_state_.reset();
#endif
        views_delegate_.reset();
        ui::ShutdownInputMethod();
#if defined(USE_AURA)
        env_.reset();
#endif
        ui::ResourceBundle::CleanupSharedInstance();
        ui::Clipboard::DestroyClipboardForCurrentThread();
    }

    WindowId CreateWindow(WindowConfig window_config) {
        CHECK_GT(window_config.width, 0);
        CHECK_GT(window_config.height, 0);

        const std::u16string title =
            window_config.title && window_config.title[0] ? window_config.title : application_name_;

        const WindowId id = next_window_id_++;
        auto state = std::make_unique<WindowState>();
        state->id = id;
        state->delegate = std::make_unique<WindowDelegate>(title, window_config.resizable);
        state->widget = std::make_unique<views::Widget>();

        views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
        params.delegate = state->delegate.get();
        const gfx::Size size(window_config.width, window_config.height);
        params.bounds = gfx::Rect(size);
        state->widget->Init(std::move(params));

        views::Widget* widget = state->widget.get();
        if (window_config.center) {
            widget->CenterWindow(size);
        }
        widget->MakeCloseSynchronous(
            base::BindOnce(&Impl::OnCloseRequested, base::Unretained(this), id));
        windows_.emplace(id, std::move(state));
        widget->Show();
        return id;
    }

    bool CloseWindow(WindowId id) {
        auto it = windows_.find(id);
        if (it == windows_.end()) {
            return false;
        }
        it->second->widget->Close();
        return true;
    }

    int Run() {
        CHECK(!run_loop_) << "Application::Run() is not reentrant";
        if (windows_.empty()) {
            return 0;
        }

        base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
        run_loop_ = &run_loop;
        run_loop.Run();
        run_loop_ = nullptr;
        return 0;
    }

    void Quit() {
        if (run_loop_) {
            run_loop_->QuitWhenIdle();
        }
    }

    bool IsRunning() const { return run_loop_ != nullptr; }

    std::size_t WindowCount() const { return windows_.size(); }

  private:
    void OnCloseRequested(WindowId id, views::Widget::ClosedReason) {
        auto it = windows_.find(id);
        if (it == windows_.end()) {
            return;
        }

        auto state = std::move(it->second);
        windows_.erase(it);
        state.reset();
        QuitIfAllWindowsClosed();
    }

    void QuitIfAllWindowsClosed() {
        if (run_loop_ && windows_.empty()) {
            // CLIENT_OWNS_WIDGET tears down Widget synchronously, but its desktop
            // native widget posts the final HWND/compositor close. Keep pumping until
            // those already-queued tasks finish so the compositor runtime sees
            // no live compositors during shutdown.
            run_loop_->QuitWhenIdle();
        }
    }

    // --- 启动序列成员 (构造顺序 = 声明顺序, 析构逆序收尾 = 冒烟 main 栈序) ---
    base::AtExitManager at_exit_; // 必须先于一切 LazyInstance/Singleton
#if BUILDFLAG(IS_WIN)
    std::unique_ptr<ui::ScopedOleInitializer> ole_;
#endif
    std::unique_ptr<base::SingleThreadTaskExecutor> task_executor_;
    std::unique_ptr<AccessibilityPlatform> accessibility_platform_;
    std::unique_ptr<CompositorRuntime> compositor_runtime_;
#if defined(USE_AURA)
    std::unique_ptr<aura::Env> env_;
#endif
    std::unique_ptr<LiewViewsDelegate> views_delegate_;
#if defined(USE_AURA)
    std::unique_ptr<wm::WMState> wm_state_;
#endif
#if BUILDFLAG(ENABLE_DESKTOP_AURA)
    std::unique_ptr<display::Screen> desktop_screen_;
#endif

    std::u16string application_name_;
    base::RunLoop* run_loop_ = nullptr;
    WindowId next_window_id_ = 1;
    std::map<WindowId, std::unique_ptr<WindowState>> windows_;
};

Application::Application(AppConfig config) : impl_(new Impl(config)) {}

Application::~Application() {
    delete impl_;
}

WindowId Application::CreateWindow(WindowConfig config) {
    return impl_->CreateWindow(std::move(config));
}

bool Application::CloseWindow(WindowId window) {
    return impl_->CloseWindow(window);
}

int Application::Run() {
    return impl_->Run();
}

void Application::Quit() {
    impl_->Quit();
}

bool Application::IsRunning() const {
    return impl_->IsRunning();
}

std::size_t Application::WindowCount() const {
    return impl_->WindowCount();
}

} // namespace liew
