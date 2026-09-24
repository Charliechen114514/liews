#ifndef LIEW_DIRECT_LAYER_TREE_FRAME_SINK_H_
#define LIEW_DIRECT_LAYER_TREE_FRAME_SINK_H_

#include "base/memory/raw_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_checker.h"
#include "cc/trees/layer_tree_frame_sink.h"
#include "components/viz/common/frame_sinks/begin_frame_source.h"
#include "components/viz/common/surfaces/parent_local_surface_id_allocator.h"
#include "components/viz/service/display/display_client.h"
#include "components/viz/service/frame_sinks/compositor_frame_sink_support.h"
#include "services/viz/public/mojom/compositing/compositor_frame_sink.mojom.h"

namespace viz {
class Display;
class FrameSinkManagerImpl;
class RasterContextProvider;
}

namespace liew {

// Connects a cc layer tree directly to the in-process Viz Display owned by
// CompositorRuntime.
class DirectLayerTreeFrameSink final
    : public cc::LayerTreeFrameSink,
      public viz::mojom::CompositorFrameSinkClient,
      public viz::ExternalBeginFrameSourceClient,
      public viz::DisplayClient {
  public:
    DirectLayerTreeFrameSink(
        const viz::FrameSinkId& frame_sink_id,
        viz::FrameSinkManagerImpl* frame_sink_manager,
        viz::Display* display,
        scoped_refptr<viz::RasterContextProvider> context_provider,
        scoped_refptr<viz::RasterContextProvider> worker_context_provider,
        scoped_refptr<base::SingleThreadTaskRunner> compositor_task_runner,
        gfx::AcceleratedWidget widget);
    DirectLayerTreeFrameSink(const DirectLayerTreeFrameSink&) = delete;
    DirectLayerTreeFrameSink& operator=(const DirectLayerTreeFrameSink&) = delete;
    ~DirectLayerTreeFrameSink() override;

    bool BindToClient(cc::LayerTreeFrameSinkClient* client) override;
    void DetachFromClient() override;
    void SubmitCompositorFrame(viz::CompositorFrame frame,
                               bool hit_test_data_changed) override;
    void DidNotProduceFrame(const viz::BeginFrameAck& ack,
                            cc::FrameSkippedReason reason) override;
    void NotifyNewLocalSurfaceIdExpectedWhilePaused() override;

    void DisplayOutputSurfaceLost() override;
    void DisplayWillDrawAndSwap(
        bool will_draw_and_swap,
        viz::AggregatedRenderPassList* render_passes) override;
    void DisplayDidDrawAndSwap() override {}
    void DisplayDidReceiveCALayerParams(
        gfx::CALayerParams ca_layer_params) override;
    void DisplayDidCompleteSwapWithSize(const gfx::Size& pixel_size) override {}
    void DisplayAddChildWindowToBrowser(gpu::SurfaceHandle child_window) override {}
    void SetWideColorEnabled(bool enabled) override {}

  private:
    void DidReceiveCompositorFrameAck(
        std::vector<viz::ReturnedResource> resources) override;
    void OnBeginFrame(const viz::BeginFrameArgs& args,
                      const viz::FrameTimingDetailsMap& timing_details,
                      std::vector<viz::ReturnedResource> resources) override;
    void ReclaimResources(std::vector<viz::ReturnedResource> resources) override;
    void OnBeginFramePausedChanged(bool paused) override;
    void OnCompositorFrameTransitionDirectiveProcessed(
        uint32_t sequence_id) override {}
    void OnSurfaceEvicted(const viz::LocalSurfaceId& local_surface_id) override {}

    void OnNeedsBeginFrames(bool needs_begin_frames) override;
    void DidReceiveCompositorFrameAckInternal(
        std::vector<viz::ReturnedResource> resources);

    THREAD_CHECKER(thread_checker_);
    std::unique_ptr<viz::CompositorFrameSinkSupport> support_;
    bool needs_begin_frames_ = false;
    const viz::FrameSinkId frame_sink_id_;
    raw_ptr<viz::FrameSinkManagerImpl> frame_sink_manager_;
    viz::ParentLocalSurfaceIdAllocator parent_local_surface_id_allocator_;
    raw_ptr<viz::Display> display_;
    gfx::AcceleratedWidget widget_;
    gfx::Size last_swap_frame_size_;
    float device_scale_factor_ = 1.f;
    std::unique_ptr<viz::ExternalBeginFrameSource> begin_frame_source_;
    viz::HitTestRegionList last_hit_test_data_;
    base::WeakPtrFactory<DirectLayerTreeFrameSink> weak_factory_{this};
};

} // namespace liew

#endif // LIEW_DIRECT_LAYER_TREE_FRAME_SINK_H_
