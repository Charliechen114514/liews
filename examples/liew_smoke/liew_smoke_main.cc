// liew 冒烟 — 显式创建窗口并运行消息循环。
// 判据: 出窗、可缩放、关窗后进程干净退出 (退出码 0)。
#include "liew/liew_app.h"

int main() {
    liew::Application app(liew::AppConfig{});
    app.CreateWindow({.title = u"liew window"});
    return app.Run();
}
