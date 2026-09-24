#pragma once

#include <cstddef>

#include "liew_app_config.h"
#include "liew_export.h"

namespace liew {

class LIEW_API Application {
  public:
    explicit Application(AppConfig config = {});
    ~Application();

    // Creates and immediately shows a top-level window. The returned id stays
    // valid until that window is closed.
    WindowId CreateWindow(WindowConfig config = {});
    bool CloseWindow(WindowId window);

    // Runs the UI message loop. It returns after Quit() is requested or the
    // last window is closed. A later call may run the application again.
    int Run();
    void Quit();

    bool IsRunning() const;
    std::size_t WindowCount() const;

  private:
    Application& operator=(const Application&) = delete;
    Application(const Application&) = delete;

    class Impl;
    Impl* impl_;
};
} // namespace liew
