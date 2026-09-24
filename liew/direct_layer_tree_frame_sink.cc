#include "liew/direct_layer_tree_frame_sink.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/notreached.h"
#include "build/build_config.h"
#include "cc/trees/layer_tree_frame_sink_client.h"
#include "components/viz/common/features.h"
#include "components/viz/common/hit_test/hit_test_region_list.h"
#include "components/viz/common/quads/compositor_frame.h"
#include "components/viz/service/display/display.h"
#include "components/viz/service/frame_sinks/frame_sink_manager_impl.h"

#if BUILDFLAG(IS_APPLE)
#include "ui/accelerated_widget_mac/ca_layer_frame_sink.h"
#endif

namespace liew {

DirectLayerTreeFrameSink::DirectLayerTreeFrameSink(
    const viz::FrameSinkId& frame_sink_id,
    viz::FrameSinkManagerImpl* frame_sink_manager,
    viz::Display* display,
    scoped_refptr<viz::RasterContextProvider> context_provider,
    scoped_refptr<viz::RasterContextProvider> worker_context_provider,
    scoped_refptr<base::SingleThreadTaskRunner> compositor_task_runner,
    gfx::AcceleratedWidget widget)
    : LayerTreeFrameSink(std::move(context_provider),
                         std::move(worker_context_provider),
                         std::move(compositor_task_runner),
                         /*shared_image_interface=*/nullptr),
      frame_sink_id_(frame_sink_id),
      frame_sink_manager_(frame_sink_manager),
      display_(display),
      widget_(widget) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

DirectLayerTreeFrameSink::~DirectLayerTreeFrameSink() {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    display_->ResetDisplayClient(this);
}

bool DirectLayerTreeFrameSink::BindToClient(
    cc::LayerTreeFrameSinkClient* client) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    if (!cc::LayerTreeFrameSink::BindToClient(client)) {
        return false;
    }

    support_ = std::make_unique<viz::CompositorFrameSinkSupport>(
        this, frame_sink_manager_, frame_sink_id_, /*is_root=*/true);
    begin_frame_source_ = std::make_unique<viz::ExternalBeginFrameSource>(this);
    client_->SetBeginFrameSource(begin_frame_source_.get());
    display_->Initialize(this, frame_sink_manager_->surface_manager());
    support_->SetUpHitTest(display_);
    return true;
}

void DirectLayerTreeFrameSink::DetachFromClient() {
    client_->SetBeginFrameSource(nullptr);
    begin_frame_source_.reset();
    support_.reset();
    cc::LayerTreeFrameSink::DetachFromClient();
}

void DirectLayerTreeFrameSink::SubmitCompositorFrame(
    viz::CompositorFrame frame,
    bool hit_test_data_changed) {
    DCHECK(frame.metadata.begin_frame_ack.has_damage);
    DCHECK(frame.metadata.begin_frame_ack.frame_id.IsSequenceValid());

    if (frame.size_in_pixels() != last_swap_frame_size_ ||
        frame.device_scale_factor() != device_scale_factor_ ||
        !parent_local_surface_id_allocator_.HasValidLocalSurfaceId()) {
        parent_local_surface_id_allocator_.GenerateId();
        last_swap_frame_size_ = frame.size_in_pixels();
        device_scale_factor_ = frame.device_scale_factor();
        display_->SetLocalSurfaceId(
            parent_local_surface_id_allocator_.GetCurrentLocalSurfaceId(),
            device_scale_factor_);
    }

    std::optional<viz::HitTestRegionList> hit_test_region_list =
        client_->BuildHitTestData();
    if (!hit_test_region_list) {
        last_hit_test_data_ = viz::HitTestRegionList();
    } else if (!hit_test_data_changed &&
               viz::HitTestRegionList::IsEqual(*hit_test_region_list,
                                               last_hit_test_data_)) {
        DCHECK(!viz::HitTestRegionList::IsEqual(*hit_test_region_list,
                                                viz::HitTestRegionList()));
        hit_test_region_list = std::nullopt;
    } else {
        last_hit_test_data_ = *hit_test_region_list;
    }

    support_->SubmitCompositorFrame(
        parent_local_surface_id_allocator_.GetCurrentLocalSurfaceId(),
        std::move(frame), std::move(hit_test_region_list));
}

void DirectLayerTreeFrameSink::DidNotProduceFrame(
    const viz::BeginFrameAck& ack,
    cc::FrameSkippedReason reason) {
    DCHECK(!ack.has_damage);
    DCHECK(ack.frame_id.IsSequenceValid());
    support_->DidNotProduceFrame(ack);
}

void DirectLayerTreeFrameSink::NotifyNewLocalSurfaceIdExpectedWhilePaused() {
    support_->NotifyNewLocalSurfaceIdExpectedWhilePaused();
}

void DirectLayerTreeFrameSink::DisplayOutputSurfaceLost() {
    client_->DidLoseLayerTreeFrameSink();
}

void DirectLayerTreeFrameSink::DisplayWillDrawAndSwap(
    bool will_draw_and_swap,
    viz::AggregatedRenderPassList* render_passes) {
    if (support_->GetHitTestAggregator()) {
        support_->GetHitTestAggregator()->Aggregate(display_->CurrentSurfaceId());
    }
}

void DirectLayerTreeFrameSink::DisplayDidReceiveCALayerParams(
    gfx::CALayerParams ca_layer_params) {
#if BUILDFLAG(IS_APPLE)
    ui::CALayerFrameSink* frame_sink =
        ui::CALayerFrameSink::FromAcceleratedWidget(widget_);
    if (frame_sink) {
        frame_sink->UpdateCALayerTree(std::move(ca_layer_params));
    }
#else
    (void)widget_;
    NOTREACHED();
#endif
}

void DirectLayerTreeFrameSink::DidReceiveCompositorFrameAck(
    std::vector<viz::ReturnedResource> resources) {
    compositor_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            &DirectLayerTreeFrameSink::DidReceiveCompositorFrameAckInternal,
            weak_factory_.GetWeakPtr(), std::move(resources)));
}

void DirectLayerTreeFrameSink::DidReceiveCompositorFrameAckInternal(
    std::vector<viz::ReturnedResource> resources) {
    client_->ReclaimResources(std::move(resources));
    if (!base::FeatureList::IsEnabled(features::kNoCompositorFrameAcks)) {
        client_->DidReceiveCompositorFrameAck();
    }
}

void DirectLayerTreeFrameSink::OnBeginFrame(
    const viz::BeginFrameArgs& args,
    const viz::FrameTimingDetailsMap& timing_details,
    std::vector<viz::ReturnedResource> resources) {
    if (!resources.empty()) {
        ReclaimResources(std::move(resources));
    }
    for (const auto& [token, details] : timing_details) {
        client_->DidPresentCompositorFrame(token, details);
    }

    if (!needs_begin_frames_) {
        DidNotProduceFrame(viz::BeginFrameAck(args, false),
                           cc::FrameSkippedReason::kNoDamage);
        return;
    }
    begin_frame_source_->OnBeginFrame(args);
}

void DirectLayerTreeFrameSink::ReclaimResources(
    std::vector<viz::ReturnedResource> resources) {
    client_->ReclaimResources(std::move(resources));
}

void DirectLayerTreeFrameSink::OnBeginFramePausedChanged(bool paused) {
    begin_frame_source_->OnSetBeginFrameSourcePaused(paused);
}

void DirectLayerTreeFrameSink::OnNeedsBeginFrames(bool needs_begin_frames) {
    needs_begin_frames_ = needs_begin_frames;
    support_->SetNeedsBeginFrame(needs_begin_frames);
}

} // namespace liew
