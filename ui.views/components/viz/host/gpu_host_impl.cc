// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/host/gpu_host_impl.h"

#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/process/process_handle.h"
#include "base/strings/strcat.h"
#include "base/system/sys_info.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread_checker.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/viz/common/buildflags.h"
#include "components/viz/common/features.h"
#include "gpu/config/gpu_driver_bug_workaround_type.h"
#include "gpu/config/gpu_feature_info.h"
#include "gpu/config/gpu_finch_features.h"
#include "gpu/config/gpu_info.h"
#include "gpu/ipc/common/gpu_client_ids.h"
#include "mojo/public/cpp/bindings/sync_call_restrictions.h"
#include "skia/buildflags.h"
#include "gpu/webgpu/dawn_commit_hash.h"  // 刀16 回收: SKIA_USE_DAWN 下 GraphiteDawnCacheVersion 需要 (纯宏头)
#include "skia/ext/skia_commit_hash.h"
#include "ui/gfx/font_render_params.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/android_info.h"
#endif

#if BUILDFLAG(IS_WIN)
#endif

#if BUILDFLAG(IS_WIN)
#include "ui/gfx/win/rendering_window_manager.h"
#elif BUILDFLAG(IS_MAC)
#include "ui/accelerated_widget_mac/window_resize_helper_mac.h"
#endif

#if BUILDFLAG(IS_OZONE)
#include "base/time/time.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/ozone/public/ozone_platform.h"
#endif

namespace viz {
namespace {

// A wrapper around gfx::FontRenderParams that checks it is set and accessed on
// the same thread.
class FontRenderParams {
 public:
  FontRenderParams(const FontRenderParams&) = delete;
  FontRenderParams& operator=(const FontRenderParams&) = delete;

  void Set(const gfx::FontRenderParams& params) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    params_ = params;
    if (gpu_host_impl_) {
      gpu_host_impl_->MaybeSendFontRenderParams();
    }
  }

  const std::optional<gfx::FontRenderParams>& Get() {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    return params_;
  }

  void SetGpuHostImpl(GpuHostImpl* gpu_host_impl) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    gpu_host_impl_ = gpu_host_impl;
  }

 private:
  friend class base::NoDestructor<FontRenderParams>;

  FontRenderParams() = default;

  ~FontRenderParams() { NOTREACHED(); }

  THREAD_CHECKER(thread_checker_);
  std::optional<gfx::FontRenderParams> params_;
  raw_ptr<GpuHostImpl> gpu_host_impl_ = nullptr;
};

FontRenderParams& GetFontRenderParams() {
  static base::NoDestructor<FontRenderParams> instance;
  return *instance;
}

#if BUILDFLAG(IS_OZONE)
bool IsHdrEnabledForGpuInfo(const gpu::GPUInfo& gpu_info) {
  return gpu_info.skia_backend_type != gpu::SkiaBackendType::kUnknown &&
         gpu_info.skia_backend_type != gpu::SkiaBackendType::kNone;
}
#endif

#if BUILDFLAG(SKIA_USE_DAWN)
[[maybe_unused]] std::string GraphiteDawnCacheVersion() {
  // We use a combination of Dawn and Skia's git hashes as the cache version.
  // - Dawn's git hash is because a new Dawn's version might change the way
  // shaders are compiled.
  // - Skia's git hash is because some cached shaders might not be used in a
  // newer version of Skia.
  return SKIA_COMMIT_HASH "_" DAWN_COMMIT_HASH;
}
#endif



}  // namespace

GpuHostImpl::InitParams::InitParams() = default;

GpuHostImpl::InitParams::InitParams(InitParams&&) = default;

GpuHostImpl::InitParams::~InitParams() = default;

GpuHostImpl::GpuHostImpl(Delegate* delegate,
                         mojo::PendingRemote<mojom::VizMain> viz_main,
                         InitParams params)
    : delegate_(delegate),
      viz_main_(std::move(viz_main)),
      params_(std::move(params)) {
  // Create a special GPU info collection service if the GPU process is used for
  // info collection only.
#if BUILDFLAG(IS_WIN)
  if (params_.info_collection_gpu_process) {
    viz_main_->CreateInfoCollectionGpuService(
        info_collection_gpu_service_remote_.BindNewPipeAndPassReceiver());
    return;
  }
#endif

  DCHECK(delegate_);

  mojo::PendingRemote<discardable_memory::mojom::DiscardableSharedMemoryManager>
      discardable_manager_remote;
  delegate_->BindDiscardableMemoryReceiver(
      discardable_manager_remote.InitWithNewPipeAndPassReceiver());

  scoped_refptr<base::SequencedTaskRunner> task_runner = nullptr;
#if BUILDFLAG(IS_MAC)
  if (params_.main_thread_task_runner->BelongsToCurrentThread())
    task_runner = ui::WindowResizeHelperMac::Get()->task_runner();
#endif

#if BUILDFLAG(IS_ANDROID)
  viz_main_->SetHostProcessId(base::GetCurrentProcId());
#endif

  mojom::GpuServiceCreationParamsPtr gpu_service_params =
      mojom::GpuServiceCreationParams::New();
#if BUILDFLAG(IS_OZONE)

#if BUILDFLAG(IS_LINUX)
  // Linux has an issue when running in single-process mode wherein
  // GetPlatformRuntimeProperties() browser-side calls can have a data race with
  // in-process GPU service initialization. The call to
  // GetPlatformRuntimeProperties() below tickles that data race. Note that
  // running in single-process mode on Linux is done only in test contexts.
  const bool can_initialize_supports_overlays =
      !params_.gpu_service_running_in_process;
#else
  constexpr bool can_initialize_supports_overlays = true;
#endif

  if (can_initialize_supports_overlays) {
    gpu_service_params->supports_overlays = ui::OzonePlatform::GetInstance()
                                                ->GetPlatformRuntimeProperties()
                                                .supports_overlays;
  }
#endif

  viz_main_->CreateGpuService(
      gpu_service_remote_.BindNewPipeAndPassReceiver(task_runner),
      gpu_host_receiver_.BindNewPipeAndPassRemote(task_runner),
      gpu_logging_receiver_.BindNewPipeAndPassRemote(task_runner),
      std::move(discardable_manager_remote),
      use_shader_cache_shm_count_.CloneRegion(), std::move(gpu_service_params));
  MaybeSendFontRenderParams();

  // (刀12: InitPersistentCache 已出局。)

#if BUILDFLAG(IS_OZONE)
  InitOzone();
#endif  // BUILDFLAG(IS_OZONE)
}

GpuHostImpl::~GpuHostImpl() {
  // (刀12: ClearPersistentCaches 已出局。)
  GetFontRenderParams().SetGpuHostImpl(nullptr);
  SendOutstandingReplies();
}

void GpuHostImpl::NotifyWorkloadIncrease() {
#if BUILDFLAG(IS_ANDROID)
  viz_main_->NotifyWorkloadIncrease();
#endif
}

// static
void GpuHostImpl::InitFontRenderParams(const gfx::FontRenderParams& params) {
  DCHECK(!GetFontRenderParams().Get());
  GetFontRenderParams().Set(params);
}

void GpuHostImpl::SetProcessId(base::ProcessId pid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(base::kNullProcessId, pid_);
  DCHECK_NE(base::kNullProcessId, pid);
  pid_ = pid;
}

void GpuHostImpl::OnProcessCrashed() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // If the GPU process crashed while compiling a shader, we may have invalid
  // cached binaries. Completely clear the shader cache to force shader binaries
  // to be re-created.
  // (刀12: shader 缓存清理段已出局。)
}

void GpuHostImpl::AddConnectionErrorHandler(base::OnceClosure handler) {
  connection_error_handlers_.push_back(std::move(handler));
}

void GpuHostImpl::BlockLiveOffscreenContexts() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::set<GURL> urls(urls_with_live_offscreen_contexts_.begin(),
                      urls_with_live_offscreen_contexts_.end());
  delegate_->BlockDomainsFrom3DAPIs(urls, gpu::DomainGuilt::kUnknown);
}

void GpuHostImpl::ConnectFrameSinkManager(
    mojo::PendingReceiver<mojom::FrameSinkManager> receiver,
    mojo::PendingRemote<mojom::FrameSinkManagerClient> client,
    const DebugRendererSettings& debug_renderer_settings) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  TRACE_EVENT0("gpu", "GpuHostImpl::ConnectFrameSinkManager");

  mojom::FrameSinkManagerParamsPtr params =
      mojom::FrameSinkManagerParams::New();
  params->restart_id = params_.restart_id;
  params->use_activation_deadline =
      params_.deadline_to_synchronize_surfaces.has_value();
  params->activation_deadline_in_frames =
      params_.deadline_to_synchronize_surfaces.value_or(0u);
  params->frame_sink_manager = std::move(receiver);
  params->frame_sink_manager_client = std::move(client);
  params->debug_renderer_settings = debug_renderer_settings;
  viz_main_->CreateFrameSinkManager(std::move(params));
}

void GpuHostImpl::EstablishGpuChannel(int client_id,
                                      uint64_t client_tracing_id,
                                      bool is_gpu_host,
                                      bool enable_extra_handles_validation,
                                      bool sync,
                                      mojo::ScopedMessagePipeHandle handle,
                                      EstablishChannelCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  TRACE_EVENT2("gpu", "GpuHostImpl::EstablishGpuChannel", "client_id",
               client_id, "is_gpu_host", is_gpu_host);
  DCHECK(!(is_gpu_host && enable_extra_handles_validation));

  shutdown_timeout_.Stop();

  if (gpu::IsReservedClientId(client_id)) {
    // The display-compositor/GrShaderCache in the gpu process uses these
    // special client ids.
    std::move(callback).Run(gpu::GPUInfo(), gpu::GpuFeatureInfo(),
                            gpu::SharedImageCapabilities(),
                            EstablishChannelStatus::kGpuAccessDenied);
    return;
  }

  channel_requests_[client_id] = std::move(callback);

  if (sync) {
    gpu::GPUInfo gpu_info;
    gpu::GpuFeatureInfo gpu_feature_info;
    gpu::SharedImageCapabilities shared_image_capabilities;
    bool success = false;
    {
      mojo::SyncCallRestrictions::ScopedAllowSyncCall scoped_allow;
      gpu_service_remote_->EstablishGpuChannel(
          client_id, client_tracing_id, is_gpu_host,
          enable_extra_handles_validation, std::move(handle), &success,
          &gpu_info, &gpu_feature_info, &shared_image_capabilities);
    }
    OnChannelEstablished(client_id, /*sync=*/true, /*success=*/success,
                         gpu_info, gpu_feature_info, shared_image_capabilities);
  } else {
    gpu_service_remote_->EstablishGpuChannel(
        client_id, client_tracing_id, is_gpu_host,
        enable_extra_handles_validation, std::move(handle),
        base::BindOnce(&GpuHostImpl::OnChannelEstablished,
                       weak_ptr_factory_.GetWeakPtr(), client_id, false));
  }

  // (刀12: SetChannelDiskCacheHandle 调用已出局。)
}

void GpuHostImpl::SetChannelClientPid(int client_id,
                                      base::ProcessId client_pid) {
  gpu_service_remote_->SetChannelClientPid(client_id, client_pid);
}

void GpuHostImpl::CloseChannel(int client_id) {
  gpu_service_remote_->CloseChannel(client_id);

  channel_requests_.erase(client_id);
}

void GpuHostImpl::CancelEstablishGpuChannel(int client_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(channel_requests_.contains(client_id));
  channel_requests_.erase(client_id);
  // Track that we cancelled a request. Mojo guarantees reply order, so the
  // next reply for this client ID will be the one from the cancelled request.
  // We track it so we can drop it in `OnChannelEstablished` and avoid it
  // consuming the callback of a subsequent request.
  cancelled_channel_requests_[client_id]++;
}

void GpuHostImpl::SendOutstandingReplies() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto& handler : connection_error_handlers_)
    std::move(handler).Run();
  connection_error_handlers_.clear();

  // Send empty channel handles for all EstablishChannel requests.
  for (auto& entry : channel_requests_) {
    std::move(entry.second)
        .Run(gpu::GPUInfo(), gpu::GpuFeatureInfo(),
             gpu::SharedImageCapabilities(),
             EstablishChannelStatus::kGpuHostInvalid);
  }
  channel_requests_.clear();
}

void GpuHostImpl::BindInterface(const std::string& interface_name,
                                mojo::ScopedMessagePipeHandle interface_pipe) {
  delegate_->BindInterface(interface_name, std::move(interface_pipe));
}

mojom::GpuService* GpuHostImpl::gpu_service() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(gpu_service_remote_.is_bound());
  return gpu_service_remote_.get();
}

#if BUILDFLAG(IS_WIN)
mojom::InfoCollectionGpuService* GpuHostImpl::info_collection_gpu_service() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(info_collection_gpu_service_remote_.is_bound());
  return info_collection_gpu_service_remote_.get();
}
#endif

#if BUILDFLAG(IS_OZONE)

void GpuHostImpl::InitOzone() {
  // Ozone needs to send the primary DRM device to GPU service as early as
  // possible to ensure the latter always has a valid device.
  // https://crbug.com/608839
  //
  // The Ozone/Wayland requires mojo communication to be established to be
  // functional with a separate gpu process. Thus, using the PlatformProperties,
  // check if there is such a requirement.
  auto interface_binder = base::BindRepeating(&GpuHostImpl::BindInterface,
                                              weak_ptr_factory_.GetWeakPtr());
  auto terminate_callback = base::BindOnce(&GpuHostImpl::TerminateGpuProcess,
                                           weak_ptr_factory_.GetWeakPtr());

  ui::OzonePlatform::GetInstance()
      ->GetGpuPlatformSupportHost()
      ->OnGpuServiceLaunched(params_.restart_id, interface_binder,
                             std::move(terminate_callback));
}

void GpuHostImpl::TerminateGpuProcess(const std::string& message) {
  delegate_->TerminateGpuProcess(message);
}

#endif  // BUILDFLAG(IS_OZONE)

std::string GpuHostImpl::GetShaderPrefixKey() {
  if (shader_prefix_key_.empty()) {
    const gpu::GPUInfo& info = delegate_->GetGPUInfo();
    const gpu::GPUInfo::GPUDevice& active_gpu = info.active_gpu();

    shader_prefix_key_ = params_.product + "-" + info.gl_vendor + "-" +
                         info.gl_renderer + "-" + active_gpu.driver_version +
                         "-" + active_gpu.driver_vendor + "-" +
                         base::SysInfo::ProcessCPUArchitecture();

#if BUILDFLAG(IS_ANDROID)
    std::string build_fp = base::android::android_info::android_build_fp();
    shader_prefix_key_ += "-" + build_fp;
#endif
  }

  return shader_prefix_key_;
}

void GpuHostImpl::OnChannelEstablished(
    int client_id,
    bool sync,
    bool success,
    const gpu::GPUInfo& gpu_info,
    const gpu::GpuFeatureInfo& gpu_feature_info,
    const gpu::SharedImageCapabilities& shared_image_capabilities) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  TRACE_EVENT0("gpu", "GpuHostImpl::OnChannelEstablished");

#if BUILDFLAG(IS_OZONE)
  ui::OzonePlatform::GetInstance()
      ->GetGpuPlatformSupportHost()
      ->OnHdrEnabledChanged(IsHdrEnabledForGpuInfo(gpu_info));
#endif  // BUILDFLAG(IS_OZONE)

  auto cancelled_it = cancelled_channel_requests_.find(client_id);
  if (cancelled_it != cancelled_channel_requests_.end()) {
    // Drop the reply from the cancelled request to prevent it from consuming
    // the callback of a subsequent request.
    cancelled_it->second--;
    if (cancelled_it->second == 0) {
      cancelled_channel_requests_.erase(cancelled_it);
    }
    return;
  }

  auto it = channel_requests_.find(client_id);
  if (it == channel_requests_.end())
    return;

  auto callback = std::move(it->second);
  channel_requests_.erase(it);

  if (!success) {
    std::move(callback).Run(gpu::GPUInfo(), gpu::GpuFeatureInfo(),
                            gpu::SharedImageCapabilities(),
                            EstablishChannelStatus::kGpuHostInvalid);
    return;
  }

  // TODO(jam): always use GPUInfo & GpuFeatureInfo from the service once we
  // know there's no issue with the ProcessHostOnUI which is the only mode
  // that currently uses it. This is because in that mode the sync mojo call
  // in the caller means we won't get the async DidInitialize() call before
  // this point, so the delegate_ methods won't have the GPU info structs yet.
  if (sync) {
    std::move(callback).Run(gpu_info, gpu_feature_info,
                            shared_image_capabilities,
                            EstablishChannelStatus::kSuccess);
  } else {
    std::move(callback).Run(
        delegate_->GetGPUInfo(), delegate_->GetGpuFeatureInfo(),
        shared_image_capabilities, EstablishChannelStatus::kSuccess);
  }
}

void GpuHostImpl::DidInitialize(
    const gpu::GPUInfo& gpu_info,
    const gpu::GpuFeatureInfo& gpu_feature_info,
    const std::optional<gpu::GPUInfo>& gpu_info_for_hardware_gpu,
    const std::optional<gpu::GpuFeatureInfo>& gpu_feature_info_for_hardware_gpu,
    const gfx::GpuExtraInfo& gpu_extra_info) {
  TRACE_EVENT0("gpu", "GpuHostImpl::DidInitialize");
  delegate_->DidInitialize(gpu_info, gpu_feature_info,
                           gpu_info_for_hardware_gpu,
                           gpu_feature_info_for_hardware_gpu, gpu_extra_info);

  gpu_uses_graphite_ =
      gpu_feature_info.status_values[gpu::GPU_FEATURE_TYPE_SKIA_GRAPHITE] ==
      gpu::kGpuFeatureStatusEnabled;

  // (刀12: persistent cache 前转段已出局。)
}

void GpuHostImpl::DidFailInitialize() {
  delegate_->DidFailInitialize();
}

void GpuHostImpl::DidCreateContextSuccessfully() {
  delegate_->DidCreateContextSuccessfully();
}

void GpuHostImpl::DidCreateOffscreenContext(const GURL& url) {
  urls_with_live_offscreen_contexts_.insert(url);
}

void GpuHostImpl::DidDestroyOffscreenContext(const GURL& url) {
  // We only want to remove *one* of the entries in the multiset for this
  // particular URL, so can't use the erase method taking a key.
  auto candidate = urls_with_live_offscreen_contexts_.find(url);
  if (candidate != urls_with_live_offscreen_contexts_.end())
    urls_with_live_offscreen_contexts_.erase(candidate);
}

void GpuHostImpl::DidDestroyChannel(int32_t client_id) {
  TRACE_EVENT0("gpu", "GpuHostImpl::DidDestroyChannel");
  // (刀12: client_id_to_caches_ 已出局。)
}

void GpuHostImpl::DidDestroyAllChannels() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!channel_requests_.empty())
    return;
  constexpr base::TimeDelta kShutDownTimeout = base::Seconds(10);
  shutdown_timeout_.Start(FROM_HERE, kShutDownTimeout,
                          base::BindOnce(&GpuHostImpl::MaybeShutdownGpuProcess,
                                         base::Unretained(this)));
}

void GpuHostImpl::MaybeShutdownGpuProcess() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(channel_requests_.empty());
  delegate_->MaybeShutdownGpuProcess();
}

void GpuHostImpl::DidLoseContext(gpu::error::ContextLostReason reason,
                                 const GURL& active_url) {
  TRACE_EVENT2("gpu", "GpuHostImpl::DidLoseContext", "reason", reason, "url",
               active_url.possibly_invalid_spec());

  if (active_url.is_empty()) {
    return;
  }

  gpu::DomainGuilt guilt = gpu::DomainGuilt::kUnknown;
  switch (reason) {
    case gpu::error::kGuilty:
      guilt = gpu::DomainGuilt::kKnown;
      break;
    // Treat most other error codes as though they had unknown provenance.
    // In practice this doesn't affect the user experience. A lost context
    // of either known or unknown guilt still causes user-level 3D APIs
    // (e.g. WebGL) to be blocked on that domain until the user manually
    // reenables them.
    case gpu::error::kUnknown:
    case gpu::error::kOutOfMemory:
    case gpu::error::kMakeCurrentFailed:
    case gpu::error::kGpuChannelLost:
    case gpu::error::kInvalidGpuMessage:
      break;
    case gpu::error::kInnocent:
      return;
  }

  std::set<GURL> urls{active_url};
  delegate_->BlockDomainsFrom3DAPIs(urls, guilt);
}

void GpuHostImpl::DisableGpuCompositing() {
  delegate_->DisableGpuCompositing();
}

// 刀16 回收: GetIsolationKey (token 类型在 blink tokens.mojom).
void GpuHostImpl::GetIsolationKey(
    int32_t client_id,
    const blink::WebGPUExecutionContextToken& token,
    GetIsolationKeyCallback cb) {
  std::string isolation_key = delegate_->GetIsolationKey(client_id, token);
  std::move(cb).Run(isolation_key);
}

void GpuHostImpl::DidUpdateGPUInfo(const gpu::GPUInfo& gpu_info) {
  delegate_->DidUpdateGPUInfo(gpu_info);
#if BUILDFLAG(IS_OZONE)
  ui::OzonePlatform::GetInstance()
      ->GetGpuPlatformSupportHost()
      ->OnHdrEnabledChanged(IsHdrEnabledForGpuInfo(gpu_info));
#endif  // BUILDFLAG(IS_OZONE)
}

#if BUILDFLAG(IS_WIN)
void GpuHostImpl::DidUpdateOverlayInfo(const gpu::OverlayInfo& overlay_info) {
  delegate_->DidUpdateOverlayInfo(overlay_info);
}

void GpuHostImpl::DidUpdateDXGIInfo(gfx::mojom::DXGIInfoPtr dxgi_info) {
  delegate_->DidUpdateDXGIInfo(std::move(dxgi_info));
}

void GpuHostImpl::AddChildWindow(gpu::SurfaceHandle parent_window,
                                 gpu::SurfaceHandle child_window) {
  if (pid_ != base::kNullProcessId) {
    gfx::RenderingWindowManager::GetInstance()->RegisterChild(
        parent_window, child_window, /*expected_child_process_id=*/pid_);
  }
}
#endif  // BUILDFLAG(IS_WIN)

void GpuHostImpl::MaybeSendFontRenderParams() {
  if (const auto& params = GetFontRenderParams().Get()) {
    viz_main_->SetRenderParams(params->subpixel_rendering,
                               params->text_contrast, params->text_gamma);
  } else {
    GetFontRenderParams().SetGpuHostImpl(this);
  }
}

gpu::GpuProcessHostShmCount* GpuHostImpl::GetShaderCacheShmCountForTesting() {
  return &use_shader_cache_shm_count_;
}

// 刀16 回收: webnn 面 (compiler context / EP providers / weights file).
#if BUILDFLAG(IS_WIN)
#endif  // BUILDFLAG(IS_WIN)

void GpuHostImpl::RecordLogMessage(int32_t severity,
                                   const std::string& header,
                                   const std::string& message) {
  delegate_->RecordLogMessage(severity, header, message);
}

}  // namespace viz
