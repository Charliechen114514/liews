#ifndef LIEW_COMPOSITOR_RUNTIME_H_
#define LIEW_COMPOSITOR_RUNTIME_H_

#include <memory>

namespace ui {
class ContextFactory;
}

namespace liew {

// CompositorRuntime — owns the in-process GPU/viz compositor stack
// (GpuRuntime with dedicated gpu/io threads, FrameSinkManager, raster
// contexts, and one Display per window) backing every liew window.
//
// Preconditions — enforced by the construction order in Application::Impl,
// NOT by this class; constructing it earlier will CHECK inside the stack:
//   * base::CommandLine initialized (GpuInit parses GPU preferences from it)
//   * mojo::core initialized (in-process channel setup)
//   * base::FeatureList initialized (GPU workaround paths read features)
//   * a UI SingleThreadTaskExecutor on the calling thread
//     (raster contexts BindToCurrentSequence here)
//
// One instance per process (CHECKed in the ctor): kClientId/kGpuClientId
// are fixed constants, a second instance would silently collide.
class CompositorRuntime {
  public:
    CompositorRuntime();
    CompositorRuntime(const CompositorRuntime&) = delete;
    CompositorRuntime& operator=(const CompositorRuntime&) = delete;
    ~CompositorRuntime();

    ui::ContextFactory* context_factory();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace liew

#endif // LIEW_COMPOSITOR_RUNTIME_H_
