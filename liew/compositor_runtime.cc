#include "liew/compositor_runtime.h"

#include <limits>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/process/process_handle.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/simple_thread.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "cc/raster/single_thread_task_graph_runner.h"
#include "components/viz/common/frame_sinks/begin_frame_args.h"
#include "components/viz/common/frame_sinks/begin_frame_source.h"
#include "components/viz/common/frame_sinks/delay_based_time_source.h"
#include "components/viz/common/surfaces/frame_sink_id_allocator.h"
#include "components/viz/common/surfaces/subtree_capture_id_allocator.h"
#include "components/viz/host/host_display_client.h"
#include "components/viz/host/host_frame_sink_manager.h"
#include "components/viz/service/display/display.h"
#include "components/viz/service/display/display_scheduler.h"
#include "components/viz/service/display/overlay_processor_stub.h"
#include "components/viz/service/display_embedder/output_surface_provider_impl.h"
#include "components/viz/service/frame_sinks/frame_sink_manager_impl.h"
#include "components/viz/service/gl/gpu_service_impl.h"
#include "gpu/command_buffer/client/shared_memory_limits.h"
#include "gpu/command_buffer/common/scheduling_priority.h"
#include "gpu/command_buffer/service/service_utils.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "gpu/ipc/service/gpu_init.h"
#include "liew/direct_layer_tree_frame_sink.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "services/viz/privileged/mojom/compositing/display_private.mojom.h"
#include "services/viz/privileged/mojom/gl/gpu_host.mojom.h"
#include "services/viz/public/cpp/gpu/command_buffer_metrics.h"
#include "services/viz/public/cpp/gpu/context_provider_command_buffer.h"
#include "ui/compositor/compositor.h"
#include "ui/display/types/display_constants.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#    include <windows.h>
#endif

namespace liew {
namespace {

constexpr uint32_t kClientId = std::numeric_limits<uint32_t>::max() / 2;
constexpr int32_t kGpuClientId = 1;

class StandaloneBeginFrameObserver final : public viz::BeginFrameObserverBase {
  public:
    ~StandaloneBeginFrameObserver() override { SetBeginFrameSource(nullptr); }

    bool OnBeginFrameDerivedImpl(const viz::BeginFrameArgs& args) override {
        if (remote_observer_.is_bound()) {
            remote_observer_->OnStandaloneBeginFrame(args);
        }
        return true;
    }

    void OnBeginFrameSourcePausedChanged(bool paused) override {}

    void SetBeginFrameSource(viz::BeginFrameSource* source) {
        TearDownObservation();
        begin_frame_source_ = source;
        SetUpObservation();
    }

    void SetObserver(mojo::PendingRemote<viz::mojom::BeginFrameObserver> observer) {
        TearDownObservation();
        remote_observer_.reset();
        remote_observer_.Bind(std::move(observer));
        SetUpObservation();
    }

  private:
    void SetUpObservation() {
        if (begin_frame_source_ && remote_observer_.is_bound() && !observing_) {
            begin_frame_source_->AddObserver(this);
            observing_ = true;
        }
    }

    void TearDownObservation() {
        if (observing_) {
            begin_frame_source_->RemoveObserver(this);
            observing_ = false;
        }
        begin_frame_source_ = nullptr;
    }

    mojo::Remote<viz::mojom::BeginFrameObserver> remote_observer_;
    raw_ptr<viz::BeginFrameSource> begin_frame_source_ = nullptr;
    bool observing_ = false;
};

class HostDisplayClient final : public viz::HostDisplayClient {
  public:
    explicit HostDisplayClient(gfx::AcceleratedWidget widget) : viz::HostDisplayClient(widget) {}

#if BUILDFLAG(IS_WIN)
    void AddChildWindowToBrowser(gpu::SurfaceHandle child_window) override {
        ::SetParent(child_window, widget());
    }
#endif
};

class GpuRuntime final {
  public:
    GpuRuntime() : gpu_thread_("liew-gpu-main"), io_thread_("liew-gpu-io") {
#if BUILDFLAG(IS_WIN)
        // D3D/DComp and the GPU service use COM from these dedicated threads.
        gpu_thread_.init_com_with_mta(true);
        io_thread_.init_com_with_mta(true);
#endif
        CHECK(gpu_thread_.Start());

        base::Thread::Options io_options(base::MessagePumpType::IO, 0);
        io_options.thread_type = base::ThreadType::kPresentation;
        CHECK(io_thread_.StartWithOptions(std::move(io_options)));

        base::WaitableEvent initialized;
        gpu_thread_.task_runner()->PostTask(FROM_HERE,
                                            base::BindOnce(&GpuRuntime::InitializeOnGpuThread,
                                                           base::Unretained(this), &initialized));
        initialized.Wait();
        CHECK(gpu_service_);
        CHECK(channel_established_);
        gpu_channel_ = gpu::GpuChannelHost::Create(
            kGpuClientId, channel_gpu_info_, channel_gpu_feature_info_,
            channel_shared_image_capabilities_, std::move(client_channel_handle_),
            io_thread_.task_runner());
        CHECK(gpu_channel_);
    }

    ~GpuRuntime() {
        gpu_channel_->DestroyChannel();
        gpu_channel_.reset();
        gpu_thread_.task_runner()->PostTask(
            FROM_HERE, base::BindOnce(&GpuRuntime::DeleteOnGpuThread, base::Unretained(this)));
        gpu_thread_.Stop();
        io_thread_.Stop();
    }

    viz::GpuServiceImpl* service() { return gpu_service_.get(); }
    scoped_refptr<gpu::GpuChannelHost> channel() { return gpu_channel_; }

  private:
    void InitializeOnGpuThread(base::WaitableEvent* initialized) {
        gpu_init_ = std::make_unique<gpu::GpuInit>();
        base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
        gpu_init_->InitializeInProcess(command_line, gpu::gles2::ParseGpuPreferences(command_line));
        CHECK(gpu_init_->init_successful());

        viz::GpuServiceImpl::InitParams init_params;
        init_params.watchdog_thread = gpu_init_->TakeWatchdogThread();
        init_params.io_runner = io_thread_.task_runner();
        init_params.vulkan_implementation = gpu_init_->vulkan_implementation();
#if BUILDFLAG(SKIA_USE_DAWN)
        init_params.dawn_context_provider = gpu_init_->TakeDawnContextProvider();
#endif

        gpu_service_ = std::make_unique<viz::GpuServiceImpl>(
            gpu_init_->gpu_preferences(), gpu_init_->gpu_info(), gpu_init_->gpu_feature_info(),
            gpu_init_->gpu_info_for_hardware_gpu(), gpu_init_->gpu_feature_info_for_hardware_gpu(),
            gpu_init_->gpu_extra_info(), std::move(init_params));

        mojo::PendingRemote<viz::mojom::GpuHost> gpu_host;
        std::ignore = gpu_host.InitWithNewPipeAndPassReceiver();
        gpu_service_->InitializeWithHost(std::move(gpu_host), gpu::GpuProcessShmCount(),
                                         gpu_init_->TakeDefaultOffscreenSurface(),
                                         viz::mojom::GpuServiceCreationParams::New());

        mojo::MessagePipe channel_pipe;
        client_channel_handle_ = std::move(channel_pipe.handle0);
        gpu_service_->EstablishGpuChannel(
            kGpuClientId, /*client_tracing_id=*/0, /*is_gpu_host=*/false,
            /*enable_extra_handles_validation=*/false, std::move(channel_pipe.handle1),
            base::BindOnce(&GpuRuntime::OnGpuChannelEstablished, base::Unretained(this)));
        CHECK(channel_established_);
        gpu_service_->SetChannelClientPid(kGpuClientId, base::GetCurrentProcId());
        initialized->Signal();
    }

    void OnGpuChannelEstablished(bool success, const gpu::GPUInfo& gpu_info,
                                 const gpu::GpuFeatureInfo& gpu_feature_info,
                                 const gpu::SharedImageCapabilities& shared_image_capabilities) {
        CHECK(success);
        channel_established_ = true;
        channel_gpu_info_ = gpu_info;
        channel_gpu_feature_info_ = gpu_feature_info;
        channel_shared_image_capabilities_ = shared_image_capabilities;
    }

    void DeleteOnGpuThread() {
        gpu_service_.reset();
        gpu_init_.reset();
    }

    base::Thread gpu_thread_;
    base::Thread io_thread_;
    std::unique_ptr<gpu::GpuInit> gpu_init_;
    std::unique_ptr<viz::GpuServiceImpl> gpu_service_;
    mojo::ScopedMessagePipeHandle client_channel_handle_;
    scoped_refptr<gpu::GpuChannelHost> gpu_channel_;
    gpu::GPUInfo channel_gpu_info_;
    gpu::GpuFeatureInfo channel_gpu_feature_info_;
    gpu::SharedImageCapabilities channel_shared_image_capabilities_;
    bool channel_established_ = false;
};

class PerCompositorData final : public viz::mojom::DisplayPrivate {
  public:
    explicit PerCompositorData(gfx::AcceleratedWidget widget) : display_client_(widget) {
#if defined(GPU_SURFACE_HANDLE_IS_ACCELERATED_WINDOW)
        surface_handle_ = widget;
#endif
    }

    ~PerCompositorData() override = default;

    void SetDisplayVisible(bool visible) override { display_->SetVisible(visible); }
    void Resize(const gfx::Size& size) override { display_->Resize(size); }
    void SetDisplayColorMatrix(const gfx::Transform& matrix) override {
        display_->SetColorMatrix(gfx::TransformToSkM44(matrix));
    }
    void SetDisplayColorSpaces(const gfx::DisplayColorSpaces& color_spaces) override {
        display_->SetDisplayColorSpaces(color_spaces);
    }
    void SetDisplayVSyncParameters(base::TimeTicks timebase, base::TimeDelta interval) override {
        if (begin_frame_source_ && interval.is_positive()) {
            begin_frame_source_->OnUpdateVSyncParameters(timebase, interval);
        }
    }
    void SetOutputIsSecure(bool secure) override { display_->SetOutputIsSecure(secure); }
#if BUILDFLAG(IS_MAC)
    void SetVSyncDisplayID(int64_t display_id) override {}
#endif
    void ForceImmediateDrawAndSwapIfPossible() override {
        display_->ForceImmediateDrawAndSwapIfPossible();
    }
    void AddVSyncParameterObserver(
        mojo::PendingRemote<viz::mojom::VSyncParameterObserver> observer) override {}
#if BUILDFLAG(IS_ANDROID)
    void UpdateRefreshRate(float refresh_rate) override {}
    void SetAdaptiveRefreshRateInfo(viz::mojom::AdaptiveRefreshRateInfoPtr info) override {}
    void PreserveChildSurfaceControls() override {}
    void SetSwapCompletionCallbackEnabled(bool enabled) override {}
#endif
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHROMEOS)
    void SetSupportedRefreshRates(const std::vector<float>& refresh_rates) override {}
#endif
    void SetDelegatedInkPointRenderer(
        mojo::PendingReceiver<gfx::mojom::DelegatedInkPointRenderer> receiver) override {
        display_->InitDelegatedInkPointRendererReceiver(std::move(receiver));
    }
    void SetStandaloneBeginFrameObserver(
        mojo::PendingRemote<viz::mojom::BeginFrameObserver> observer) override {
        standalone_observer_.SetObserver(std::move(observer));
    }
    void SetMaxVSyncAndVrr(std::optional<base::TimeDelta> max_vsync_interval,
                           display::VariableRefreshRateState vrr_state) override {
        if (begin_frame_source_) {
            begin_frame_source_->SetMaxVrrInterval(max_vsync_interval);
        }
    }

    void Bind(mojo::PendingAssociatedReceiver<viz::mojom::DisplayPrivate> receiver) {
        receiver_.reset();
        receiver_.Bind(std::move(receiver));
    }

    void SetBeginFrameSource(std::unique_ptr<viz::DelayBasedBeginFrameSource> begin_frame_source) {
        begin_frame_source_ = std::move(begin_frame_source);
        standalone_observer_.SetBeginFrameSource(begin_frame_source_.get());
    }

    void SetDisplay(std::unique_ptr<viz::Display> display) { display_ = std::move(display); }

    gpu::SurfaceHandle surface_handle() const { return surface_handle_; }
    viz::mojom::DisplayClient* display_client() { return &display_client_; }
    viz::BeginFrameSource* begin_frame_source() { return begin_frame_source_.get(); }
    viz::Display* display() { return display_.get(); }

  private:
    HostDisplayClient display_client_;
    gpu::SurfaceHandle surface_handle_ = gpu::kNullSurfaceHandle;
    StandaloneBeginFrameObserver standalone_observer_;
    std::unique_ptr<viz::DelayBasedBeginFrameSource> begin_frame_source_;
    std::unique_ptr<viz::Display> display_;
    mojo::AssociatedReceiver<viz::mojom::DisplayPrivate> receiver_{this};
};

} // namespace

class CompositorRuntime::Impl final : public ui::ContextFactory {
  public:
    Impl()
        : gpu_runtime_(std::make_unique<GpuRuntime>()),
          output_surface_provider_(std::make_unique<viz::OutputSurfaceProviderImpl>(
              gpu_runtime_->service(), /*headless=*/false)),
          host_frame_sink_manager_(std::make_unique<viz::HostFrameSinkManager>()),
          frame_sink_id_allocator_(kClientId) {
        viz::FrameSinkManagerImpl::InitParams frame_sink_params(output_surface_provider_.get());
        frame_sink_params.gpu_service = gpu_runtime_->service();
        frame_sink_manager_ =
            std::make_unique<viz::FrameSinkManagerImpl>(std::move(frame_sink_params));
        frame_sink_manager_->SetLocalClient(host_frame_sink_manager_.get());
        host_frame_sink_manager_->SetLocalManager(frame_sink_manager_.get());
        task_graph_runner_.Start("liew-compositor-raster", base::SimpleThread::Options());

        worker_context_provider_ = viz::ContextProviderCommandBuffer::CreateForRaster(
            gpu_runtime_->channel(), /*stream_id=*/0, gpu::SchedulingPriority::kNormal,
            GURL("liew://raster-worker"),
            /*automatic_flushes=*/true, /*support_locking=*/true,
            gpu::SharedMemoryLimits::ForGPURasterContext(),
            viz::command_buffer_metrics::ContextType::BROWSER_RASTER_WORKER);
        CHECK_EQ(worker_context_provider_->BindToCurrentSequence(), gpu::ContextResult::kSuccess);
    }

    ~Impl() override {
        CHECK(per_compositor_data_.empty());
        task_graph_runner_.Shutdown();
    }

    void CreateLayerTreeFrameSink(base::WeakPtr<ui::Compositor> compositor) override {
        CHECK(compositor);

        auto it = per_compositor_data_.find(compositor.get());
        if (it == per_compositor_data_.end()) {
            it = per_compositor_data_
                     .emplace(compositor.get(),
                              std::make_unique<PerCompositorData>(compositor->widget()))
                     .first;
        } else if (it->second->begin_frame_source()) {
            frame_sink_manager_->UnregisterBeginFrameSource(it->second->begin_frame_source());
        }
        PerCompositorData* data = it->second.get();

        mojo::AssociatedRemote<viz::mojom::DisplayPrivate> display_private;
        data->Bind(display_private.BindNewEndpointAndPassDedicatedReceiver());

        auto gpu_dependency = output_surface_provider_->CreateGpuDependency(
            /*gpu_compositing=*/true, data->surface_handle());
        CHECK(gpu_dependency);
        std::unique_ptr<viz::OutputSurface> output_surface =
            output_surface_provider_->CreateOutputSurface(
                data->surface_handle(), /*gpu_compositing=*/true, data->display_client(),
                gpu_dependency.get(), renderer_settings_, &debug_settings_);
        CHECK(output_surface);

        auto time_source =
            std::make_unique<viz::DelayBasedTimeSource>(compositor->task_runner().get());
        time_source->SetTimebaseAndInterval(base::TimeTicks(),
                                            viz::BeginFrameArgs::DefaultInterval());
        auto begin_frame_source = std::make_unique<viz::DelayBasedBeginFrameSource>(
            std::move(time_source), viz::BeginFrameSource::kNotRestartableId);
        auto scheduler = std::make_unique<viz::DisplayScheduler>(
            begin_frame_source.get(), compositor->task_runner().get(),
            output_surface->capabilities().pending_swap_params,
            /*hint_session_factory=*/nullptr);

        data->SetDisplay(std::make_unique<viz::Display>(
            gpu_runtime_->service()->shared_image_manager(),
            gpu_runtime_->service()->gpu_scheduler(), renderer_settings_, &debug_settings_,
            compositor->frame_sink_id(), std::move(gpu_dependency), std::move(output_surface),
            std::make_unique<viz::OverlayProcessorStub>(), std::move(scheduler),
            compositor->task_runner()));
        frame_sink_manager_->RegisterBeginFrameSource(begin_frame_source.get(),
                                                      compositor->frame_sink_id());
        data->SetBeginFrameSource(std::move(begin_frame_source));

        auto frame_sink = std::make_unique<DirectLayerTreeFrameSink>(
            compositor->frame_sink_id(), frame_sink_manager_.get(), data->display(),
            SharedMainThreadRasterContextProvider(), worker_context_provider_,
            compositor->task_runner(), compositor->widget());
        compositor->SetLayerTreeFrameSink(std::move(frame_sink), std::move(display_private));
        data->Resize(compositor->size());
    }

    scoped_refptr<viz::RasterContextProvider> SharedMainThreadRasterContextProvider() override {
        if (main_context_provider_ && !main_context_provider_->IsLost()) {
            return main_context_provider_;
        }
        main_context_provider_ = viz::ContextProviderCommandBuffer::CreateForRaster(
            gpu_runtime_->channel(), /*stream_id=*/0, gpu::SchedulingPriority::kNormal,
            GURL("liew://compositor"),
            /*automatic_flushes=*/false, /*support_locking=*/false,
            gpu::SharedMemoryLimits::ForMailboxContext(),
            viz::command_buffer_metrics::ContextType::BROWSER_COMPOSITOR);
        if (main_context_provider_->BindToCurrentSequence() != gpu::ContextResult::kSuccess) {
            main_context_provider_.reset();
        }
        return main_context_provider_;
    }

    void RemoveCompositor(ui::Compositor* compositor) override {
        auto it = per_compositor_data_.find(compositor);
        if (it == per_compositor_data_.end()) {
            return;
        }
        frame_sink_manager_->UnregisterBeginFrameSource(it->second->begin_frame_source());
        per_compositor_data_.erase(it);
    }

    cc::TaskGraphRunner* GetTaskGraphRunner() override { return &task_graph_runner_; }

    viz::FrameSinkId AllocateFrameSinkId() override {
        return frame_sink_id_allocator_.NextFrameSinkId();
    }

    viz::SubtreeCaptureId AllocateSubtreeCaptureId() override {
        return subtree_capture_id_allocator_.NextSubtreeCaptureId();
    }

    viz::HostFrameSinkManager* GetHostFrameSinkManager() override {
        return host_frame_sink_manager_.get();
    }

  private:
    std::unique_ptr<GpuRuntime> gpu_runtime_;
    std::unique_ptr<viz::OutputSurfaceProviderImpl> output_surface_provider_;
    std::unique_ptr<viz::FrameSinkManagerImpl> frame_sink_manager_;
    std::unique_ptr<viz::HostFrameSinkManager> host_frame_sink_manager_;
    scoped_refptr<viz::ContextProviderCommandBuffer> main_context_provider_;
    scoped_refptr<viz::ContextProviderCommandBuffer> worker_context_provider_;
    cc::SingleThreadTaskGraphRunner task_graph_runner_;
    viz::FrameSinkIdAllocator frame_sink_id_allocator_;
    viz::SubtreeCaptureIdAllocator subtree_capture_id_allocator_;
    viz::RendererSettings renderer_settings_;
    viz::DebugRendererSettings debug_settings_;
    std::unordered_map<ui::Compositor*, std::unique_ptr<PerCompositorData>> per_compositor_data_;
};

CompositorRuntime::CompositorRuntime() : impl_(std::make_unique<Impl>()) {}

CompositorRuntime::~CompositorRuntime() = default;

ui::ContextFactory* CompositorRuntime::context_factory() {
    return impl_.get();
}

} // namespace liew
