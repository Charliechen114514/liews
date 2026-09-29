// liew gallery — 控件示例浏览器;每桥一个控件加一页。
#include <functional>
#include <memory>
#include <vector>

#include "liew/app/liew_app.h"
#include "liew/app/liew_app_config_builder.h"
#include "liew/base/gui/color.h"
#include "liew/base/insets.h"
#include "liew/widget/layout/box_layout.h"
#include "liew/widget/layout/filled_layout.h"
#include "liew/widget/widgets/button.h"
#include "liew/widget/widgets/checkbox.h"
#include "liew/widget/widgets/combobox.h"
#include "liew/widget/widgets/container.h"
#include "liew/widget/widgets/label.h"
#include "liew/widget/widgets/progress_bar.h"
#include "liew/widget/widgets/radio_button.h"
#include "liew/widget/widgets/scroll_view.h"
#include "liew/widget/widgets/slider.h"
#include "liew/widget/widgets/tabbed_pane.h"
#include "liew/widget/widgets/text_field.h"
#include "liew/widget/widgets/toggle_button.h"
#include "liew/window/dialog.h"
#include "liew/window/liew_window.h"
#include "liew/window/menu.h"

namespace {

using PageFactory = std::function<std::unique_ptr<liew::Widget>()>;

struct GalleryPage {
    const char16_t* name;
    PageFactory create;
};

std::unique_ptr<liew::Widget> MakeButtonsPage(liew::Window& window) {
    auto page = std::make_unique<liew::Container>();
    page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                    .spacing = 8,
                                    .padding = liew::Insets(12, 12, 12, 12)});

    auto* plain = page->AddChild(std::make_unique<liew::Button>(u"Plain button"));
    auto* styled = page->AddChild(std::make_unique<liew::Button>(u"Styled button"));
    auto* quit = page->AddChild(std::make_unique<liew::Button>(u"Close gallery"));

    plain->SetCallback({.on_click = [plain] { plain->SetText(u"clicked!"); }});
    styled->SetStyle({
        .text_style = {.size = 16, .bold = true},
        .box_style = {.background = liew::Color::Rgb(0x2B, 0x57, 0x9A),
                      .corner_radius = 6.0f,
                      .padding = liew::Insets(8, 16, 8, 16)},
    });
    styled->SetCallback({.on_click = [styled] { styled->SetText(u"nice!"); }});
    quit->SetCallback({.on_click = [&window] { window.Close(); }});
    return page;
}

std::unique_ptr<liew::Widget> MakeLayoutsPage() {
    auto page = std::make_unique<liew::Container>();
    page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                    .spacing = 12,
                                    .padding = liew::Insets(12, 12, 12, 12)});

    auto* row = page->AddChild(std::make_unique<liew::Container>());
    row->SetLayout(liew::BoxLayout{.spacing = 8});
    for (int i = 0; i < 3; ++i) {
        auto* cell = row->AddChild(std::make_unique<liew::Button>(u"cell"));
        cell->SetStyle({.box_style = {.padding = liew::Insets(4, 12, 4, 12)}});
    }

    auto* column = page->AddChild(std::make_unique<liew::Container>());
    column->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical, .spacing = 4});
    for (int i = 0; i < 3; ++i) {
        column->AddChild(std::make_unique<liew::Button>(u"item"));
    }
    return page;
}

std::unique_ptr<liew::Widget> MakeStylesPage() {
    auto page = std::make_unique<liew::Container>();
    page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                    .spacing = 10,
                                    .padding = liew::Insets(12, 12, 12, 12)});

    auto* big = page->AddChild(std::make_unique<liew::Button>(u"big & bold"));
    big->SetStyle({.text_style = {.size = 20, .bold = true}});

    auto* danger = page->AddChild(std::make_unique<liew::Button>(u"danger"));
    danger->SetStyle({
        .text_style = {.color = liew::Color::Rgb(0xF2, 0x8B, 0x8B)},
        .box_style = {.background = liew::Color::Rgb(0x3A, 0x2D, 0x2D),
                      .corner_radius = 6.0f,
                      .padding = liew::Insets(6, 14, 6, 14)},
    });

    auto* marked = page->AddChild(std::make_unique<liew::Button>(u"italic & underline"));
    marked->SetStyle({.text_style = {.italic = true, .underline = true},
                      .box_style = {.padding = liew::Insets(6, 14, 6, 14)}});
    return page;
}

std::unique_ptr<liew::Widget> MakeSelectionPage() {
    auto page = std::make_unique<liew::Container>();
    page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                    .spacing = 10,
                                    .padding = liew::Insets(12, 12, 12, 12)});

    auto* status = page->AddChild(std::make_unique<liew::Label>(u"try the controls"));
    status->SetStyle({.text_style = {.color = liew::Color::Rgb(0x9A, 0x9A, 0xB0)}});

    auto* check = page->AddChild(std::make_unique<liew::Checkbox>(u"checkbox"));
    check->SetCallback({.on_checked_changed = [status](bool checked) {
        status->SetText(checked ? u"checkbox: on" : u"checkbox: off");
    }});

    auto* group = page->AddChild(std::make_unique<liew::Container>());
    group->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical, .spacing = 4});
    auto* radio_a = group->AddChild(std::make_unique<liew::RadioButton>(u"option A"));
    auto* radio_b = group->AddChild(std::make_unique<liew::RadioButton>(u"option B"));
    radio_a->SetChecked(true);
    radio_b->SetCallback(
        {.on_checked_changed = [status](bool) { status->SetText(u"radio: option B"); }});

    auto* toggle_row = page->AddChild(std::make_unique<liew::Container>());
    toggle_row->SetLayout(liew::BoxLayout{.spacing = 12});
    toggle_row->AddChild(std::make_unique<liew::Label>(u"toggle:"));
    auto* toggle = toggle_row->AddChild(std::make_unique<liew::ToggleButton>());
    toggle->SetAccessibleName(u"toggle demo");
    toggle->SetCallback({.on_toggled = [status](bool on) {
        status->SetText(on ? u"toggle: on" : u"toggle: off");
    }});
    return page;
}

std::unique_ptr<liew::Widget> MakeProgressPage() {
    auto page = std::make_unique<liew::Container>();
    page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                    .spacing = 10,
                                    .padding = liew::Insets(12, 12, 12, 12)});

    auto* title = page->AddChild(std::make_unique<liew::Label>(u"progress"));
    title->SetStyle({.text_style = {.size = 16, .bold = true}});

    auto* bar = page->AddChild(std::make_unique<liew::ProgressBar>());
    bar->SetValue(0.33);

    auto* row = page->AddChild(std::make_unique<liew::Container>());
    row->SetLayout(liew::BoxLayout{.spacing = 8});
    auto* low = row->AddChild(std::make_unique<liew::Button>(u"33%"));
    auto* mid = row->AddChild(std::make_unique<liew::Button>(u"66%"));
    auto* full = row->AddChild(std::make_unique<liew::Button>(u"100%"));
    auto* infinity = row->AddChild(std::make_unique<liew::Button>(u"inf"));
    low->SetCallback({.on_click = [bar] { bar->SetValue(0.33); }});
    mid->SetCallback({.on_click = [bar] { bar->SetValue(0.66); }});
    full->SetCallback({.on_click = [bar] { bar->SetValue(1.0); }});
    infinity->SetCallback({.on_click = [bar] { bar->SetValue(-1.0); }});
    return page;
}

std::unique_ptr<liew::Widget> MakeInputsPage() {
    auto page = std::make_unique<liew::Container>();
    page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                    .spacing = 10,
                                    .padding = liew::Insets(12, 12, 12, 12)});

    auto* status = page->AddChild(std::make_unique<liew::Label>(u"type / drag / pick"));
    status->SetStyle({.text_style = {.color = liew::Color::Rgb(0x9A, 0x9A, 0xB0)}});

    auto* field = page->AddChild(std::make_unique<liew::TextField>());
    field->SetPlaceholder(u"type here (CJK IME welcome)");
    field->SetCallback({.on_changed = [status](const char16_t* text) { status->SetText(text); }});

    auto* slider = page->AddChild(std::make_unique<liew::Slider>());
    slider->SetValue(0.5f);
    slider->SetCallback({.on_value_changed = [status](float value) {
        int percent = static_cast<int>(value * 100.0f + 0.5f);
        std::u16string text;
        do {
            text.insert(text.begin(), u'0' + percent % 10);
            percent /= 10;
        } while (percent > 0);
        text += u'%';
        status->SetText(text.c_str());
    }});
    slider->SetAccessibleName(u"slider demo");

    auto* combo = page->AddChild(
        std::make_unique<liew::Combobox>(std::vector<std::u16string>{u"red", u"green", u"blue"}));
    combo->SetCallback({.on_selected_changed = [status](int index) {
        const char16_t* names[] = {u"red", u"green", u"blue"};
        status->SetText(index >= 0 && index < 3 ? names[index] : u"none");
    }});
    return page;
}

std::unique_ptr<liew::Widget> MakeScrollTabPanePage() {
    auto page = std::make_unique<liew::Container>();
    page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                    .spacing = 10,
                                    .padding = liew::Insets(12, 12, 12, 12)});

    auto* tabs = page->AddChild(std::make_unique<liew::TabbedPane>());

    auto scroll_page = std::make_unique<liew::Container>();
    scroll_page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                           .spacing = 6,
                                           .padding = liew::Insets(8, 8, 8, 8)});
    for (int i = 1; i <= 30; ++i) {
        std::u16string line = u"scrollable line ";
        if (i >= 10)
            line += static_cast<char16_t>(u'0' + i / 10);
        line += static_cast<char16_t>(u'0' + i % 10);
        scroll_page->AddChild(std::make_unique<liew::Label>(line.c_str()));
    }
    auto* scroll = tabs->AddTab(u"Scroll", std::make_unique<liew::ScrollView>());
    scroll->SetContent(std::move(scroll_page));

    auto button_page = std::make_unique<liew::Container>();
    button_page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                           .spacing = 8,
                                           .padding = liew::Insets(8, 8, 8, 8)});
    auto* ping = button_page->AddChild(std::make_unique<liew::Button>(u"ping"));
    ping->SetCallback({.on_click = [ping] { ping->SetText(u"pong"); }});
    auto* reset = button_page->AddChild(std::make_unique<liew::Button>(u"reset"));
    reset->SetCallback({.on_click = [ping] { ping->SetText(u"ping"); }});
    tabs->AddTab(u"Buttons", std::move(button_page));

    tabs->SelectTabAt(0);
    return page;
}

std::unique_ptr<liew::Widget> MakeDialogsPage() {
    auto page = std::make_unique<liew::Container>();
    page->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical,
                                    .spacing = 10,
                                    .padding = liew::Insets(12, 12, 12, 12)});

    auto* status =
        page->AddChild(std::make_unique<liew::Label>(u"menus & dialogs"));
    status->SetStyle({.text_style = {.color = liew::Color::Rgb(0x9A, 0x9A, 0xB0)}});

    auto* menu_button = page->AddChild(std::make_unique<liew::Button>(u"popup menu"));
    menu_button->SetCallback({.on_click = [status, menu_button] {
        liew::Menu::Show(
            {
                {u"say hi", [status] { status->SetText(u"menu: hi!"); }, true, false},
                {u"checked item", nullptr, true, true},
                {nullptr, nullptr, true, false},
                {u"disabled item", nullptr, false, false},
                {nullptr, nullptr, true, false},
                {u"close menu", nullptr, true, false},
            },
            menu_button, 0, 30);
    }});

    auto* dialog_button =
        page->AddChild(std::make_unique<liew::Button>(u"open modal dialog"));
    dialog_button->SetCallback({.on_click = [dialog_button] {
        liew::Dialog::Show(
            dialog_button, {.title = u"liew dialog", .size = {320, 180}},
            [](liew::Widget* root) {
                auto* label =
                    root->AddChild(std::make_unique<liew::Label>(u"a modal dialog"));
                label->SetStyle({.text_style = {.size = 14, .bold = true}});
                auto* close =
                    root->AddChild(std::make_unique<liew::Button>(u"close me"));
                close->SetCallback(
                    {.on_click = [close] { close->CloseWindow(); }});
            });
    }});
    return page;
}

} // namespace

int main() {
    liew::Application app(liew::AppConfigBuilder::ForArguments(0, nullptr).Build());
    liew::Window window(app, {.title = u"liew gallery", .size = {960, 640}});

    liew::Widget* root = window.RootView();
    root->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Horizontal,
                                    .spacing = 8,
                                    .padding = liew::Insets(8, 8, 8, 8)});

    auto* nav = root->AddChild(std::make_unique<liew::Container>());
    nav->SetLayout(liew::BoxLayout{.orientation = liew::Orientation::Vertical, .spacing = 4});

    auto* content = root->AddChild(std::make_unique<liew::Container>());
    content->SetLayout(liew::FilledLayout{});

    std::vector<GalleryPage> pages = {
        {u"Inputs", [] { return MakeInputsPage(); }},
        {u"Containers", [] { return MakeScrollTabPanePage(); }},
        {u"Dialogs", [] { return MakeDialogsPage(); }},
        {u"Selection", [] { return MakeSelectionPage(); }},
        {u"Buttons", [&] { return MakeButtonsPage(window); }},
        {u"Progress", [] { return MakeProgressPage(); }},
        {u"Layouts", [] { return MakeLayoutsPage(); }},
        {u"Styles", [] { return MakeStylesPage(); }},
    };

    for (size_t i = 0; i < pages.size(); ++i) {
        auto* nav_button = nav->AddChild(std::make_unique<liew::Button>(pages[i].name));
        nav_button->SetCallback({.on_click = [content, &pages, i] {
            content->ClearChildren();
            content->AddChild(pages[i].create());
        }});
    }

    content->AddChild(pages.front().create());
    return app.Run();
}
