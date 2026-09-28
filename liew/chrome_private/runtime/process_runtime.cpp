#include "liew/chrome_private/runtime/process_runtime.h"

#include <atomic>
#include <utility>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/i18n/icu_util.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "liew/chrome_private/runtime/discardable_memory.h"
#include "mojo/core/embedder/embedder.h"
#include "ui/gfx/font_util.h"

#if BUILDFLAG(IS_WIN)
#    include "ui/base/win/scoped_ole_initializer.h"
#endif

namespace liew::internal {
namespace {

// One instance at a time; sequential re-creation after teardown is allowed
// (mirrors Application's construct-run-destroy-construct cycle).
std::atomic_bool g_process_runtime_alive{false};

}  // namespace

struct ProcessRuntime::State {
    // Must be the first member: before any LazyInstance/Singleton in the
    // process. Declaration order is the ownership/teardown order.
    base::AtExitManager at_exit;
#if BUILDFLAG(IS_WIN)
    std::unique_ptr<ui::ScopedOleInitializer> ole;
#endif
};

ProcessRuntime::ProcessRuntime(const Options& options) : state_(std::make_unique<State>()) {
    bool expected = false;
    CHECK(g_process_runtime_alive.compare_exchange_strong(expected, true))
        << "Only one liew::internal::ProcessRuntime may exist at a time";

    if (!base::CommandLine::InitializedForCurrentProcess()) {
        base::CommandLine::Init(options.argument_count, options.argument_params);
    }

#if BUILDFLAG(IS_WIN)
    state_->ole = std::make_unique<ui::ScopedOleInitializer>();
#endif
    // FeatureList is initialized on every platform: GPU paths consume it
    // unconditionally (e.g. direct_layer_tree_frame_sink.cc), matching the
    // upstream views embedder (examples_main_proc.cc initializes it bare).
    if (!base::FeatureList::GetInstance()) {
        auto* cmd = base::CommandLine::ForCurrentProcess();
        base::FeatureList::InitInstance(cmd->GetSwitchValueASCII("enable-features"),
                                        cmd->GetSwitchValueASCII("disable-features"));
    }

    mojo::core::Init();
    base::i18n::InitializeICU();
    if (!base::DiscardableMemoryAllocator::HasInstance()) {
        // Standalone embedders have no browser-side discardable-memory Mojo
        // service. Install the process-wide heap allocator; the NoDestructor
        // instance intentionally has application lifetime.
        static base::NoDestructor<PlainMemAllocator> allocator(options.discardable_budget_bytes);
        base::DiscardableMemoryAllocator::SetInstance(allocator.get());
    }
    gfx::InitializeFonts();
}

ProcessRuntime::~ProcessRuntime() {
    // State members tear down in reverse order: OLE first, AtExitManager last.
    // mojo/ICU/fonts/FeatureList/allocator intentionally keep running.
    g_process_runtime_alive.store(false);
}

}  // namespace liew::internal
