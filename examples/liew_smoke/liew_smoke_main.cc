// liew 冒烟 — 构造 Application 即: 启动序列 + 一个居中窗口 + 跑消息循环
// 到窗口关闭。判据: 出窗、可缩放、关窗后进程干净退出 (退出码 0)。
#include "liew/liew_app.h"

int main() {
  liew::Application app(liew::AppConfig{});
  return 0;
}
