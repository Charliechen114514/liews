#pragma once

#include <cstddef>

#include "liew_app_config.h"
#include "liew/liew_export.h"

// Self-defence: windows.h (pulled in transitively by UI headers) defines a
// CreateWindow macro that would mangle the CreateWindow() declaration below.
// Guarding here makes include order irrelevant for every consumer.
#ifdef CreateWindow
#    undef CreateWindow
#endif

namespace liew {

class Window;

class LIEW_API Application {
  public:
    explicit Application(AppConfig config);
    ~Application();

    Application& GetApplicationReference() { return *this; }

    // Runs the UI message loop. It returns after Quit() is requested or the
    // last window is closed (framework semantics — windows are the reason
    // for the app to keep running). A later call may run the app again.
    int Run();
    void Quit();

    bool IsRunning() const;

  private:
    Application& operator=(const Application&) = delete;
    Application(const Application&) = delete;

    friend class Window;  // Window 实现经 impl_ 取窗口子系统与应用名

    class Impl;
    Impl* impl_;
};
} // namespace liew
