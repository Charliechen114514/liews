#pragma once

#include "liew_app_config.h"
#include "liew_export.h"
namespace liew {

class LIEW_API Application {
  public:
    explicit Application(AppConfig config);
    ~Application(); // pimpl (Impl 前置声明) 要求析构在 .cc 落地

  private:
    Application() = delete;
    Application& operator=(const Application&) = delete;
    Application(const Application&) = delete;

    // pimpl — 实现细节 (启动序列持有), 见 liew/app.cc; API 面不外露。
    class Impl;
    Impl* impl_;
};
} // namespace liew
