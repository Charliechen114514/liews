// liew 冒烟 — 显式创建窗口并运行消息循环。
// 判据: 出窗、可缩放、关窗后进程干净退出 (退出码 0)。
#include <memory>

#include "liew/app/liew_app.h"
#include "liew/app/liew_app_config_builder.h"
#include "liew/widget/layout/box_layout.h"
#include "liew/widget/widgets/button.h"
#include "liew/window/liew_window.h"

int main() {
    liew::Application app(liew::AppConfigBuilder::ForArguments(0, nullptr).Build());
    liew::Window window(app, {.title = u"liew window"});

    liew::Widget* root = window.RootView();
    root->SetLayout(liew::BoxLayout{
        .orientation = liew::Orientation::Vertical,
        .spacing = 8,
        .padding = liew::Insets(12, 12, 12, 12),
    });

    auto* hello_button =
        root->AddChild(std::make_unique<liew::Button>(u"Hello liew"));
    auto* quit_button = root->AddChild(std::make_unique<liew::Button>(u"Quit"));

    hello_button->SetStyle({
        .text_style = {.size = 16, .bold = true},
        .box_style = {.padding = liew::Insets(8, 16, 8, 16)},
    });
    quit_button->SetStyle({
        .text_style = {.color = liew::Color::Rgb(0xF2, 0x8B, 0x8B)},
        .box_style = {.background = liew::Color::Rgb(0x3A, 0x2D, 0x2D),
                      .corner_radius = 6.0f,
                      .padding = liew::Insets(8, 16, 8, 16)},
    });

    hello_button->SetCallback({.on_click = [hello_button] { hello_button->SetText(u"clicked!"); }});
    quit_button->SetCallback({.on_click = [&window] { window.Close(); }});

    return app.Run();
}
