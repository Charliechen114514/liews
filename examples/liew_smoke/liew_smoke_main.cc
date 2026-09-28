// liew 冒烟 — 显式创建窗口并运行消息循环。
// 判据: 出窗、可缩放、关窗后进程干净退出 (退出码 0)。
#include "liew/app/liew_app.h"
#include "liew/app/liew_app_config_builder.h"
#include "liew/window/liew_window.h"

int main() {
    liew::Application app(liew::AppConfigBuilder::ForArguments(0, nullptr).Build());
    liew::Window window(app, {.title = u"liew window"});
    return app.Run();
}
