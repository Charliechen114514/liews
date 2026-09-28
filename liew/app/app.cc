// liew::Application — 公共门面:六方法转发给 chrome_private 内的装配实现。
// 框架公共层(app/、base/)零 chrome 树头 —— chrome 依赖全部封死在
// liew/chrome_private/ 内,本文件是 app 层唯一 include 装配实现的地方。
#include "liew/app/liew_app.h"

#include <utility>

#include "liew/chrome_private/app/application_impl.h"

namespace liew {

Application::Application(AppConfig config) : impl_(new Impl(config)) {}

Application::~Application() {
    delete impl_;
}

int Application::Run() {
    return impl_->Run();
}

void Application::Quit() {
    impl_->Quit();
}

bool Application::IsRunning() const {
    return impl_->IsRunning();
}

} // namespace liew
