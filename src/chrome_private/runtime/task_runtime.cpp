#include "src/chrome_private/runtime/task_runtime.h"

#include <algorithm>

#include "base/check.h"
#include "base/system/sys_info.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread_checker.h"
#include "build/build_config.h"

namespace liew::internal {

struct TaskRuntime::State {
    THREAD_CHECKER(thread_checker);
    std::unique_ptr<base::SingleThreadTaskExecutor> task_executor;
};

TaskRuntime::TaskRuntime() : state_(std::make_unique<State>()) {
    DCHECK_CALLED_ON_VALID_THREAD(state_->thread_checker);
    // Refuse to shadow an existing default executor on this thread — the
    // runtime must own the UI sequence (same discipline as upstream
    // TaskEnvironment).
    CHECK(!base::SingleThreadTaskRunner::HasCurrentDefault());

    state_->task_executor =
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
}

TaskRuntime::~TaskRuntime() {
    DCHECK_CALLED_ON_VALID_THREAD(state_->thread_checker);
    // task_executor tears down with State. The ThreadPoolInstance
    // intentionally never shuts down (threads live until process exit).
}

}  // namespace liew::internal
