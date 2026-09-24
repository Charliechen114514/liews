#ifndef LIEW_COMPOSITOR_RUNTIME_H_
#define LIEW_COMPOSITOR_RUNTIME_H_

#include <memory>

namespace ui {
class ContextFactory;
}

namespace liew {

// Owns the in-process Viz software compositor used by liew windows.
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
