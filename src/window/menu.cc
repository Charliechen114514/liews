#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "liew/window/menu.h"
#include "src/widget/widget_impl.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace liew {
namespace {

struct MenuEntry {
    std::u16string text;
    std::function<void()> on_click;
    bool enabled = true;
    bool checked = false;
};

class ModelDelegate final : public ui::SimpleMenuModel::Delegate {
  public:
    std::vector<MenuEntry>* entries = nullptr;

    // ui::SimpleMenuModel::Delegate:
    bool IsCommandIdEnabled(int command_id) const override {
        return (*entries)[static_cast<size_t>(command_id)].enabled;
    }
    bool IsCommandIdChecked(int command_id) const override {
        return (*entries)[static_cast<size_t>(command_id)].checked;
    }
    bool GetAcceleratorForCommandId(int command_id,
                                    ui::Accelerator* accelerator) const override {
        return false;
    }
    void ExecuteCommand(int command_id, int event_flags) override {
        auto& entry = (*entries)[static_cast<size_t>(command_id)];
        if (entry.on_click)
            entry.on_click();
    }
};

}  // namespace

class Menu::Impl {
  public:
    std::vector<MenuEntry> entries;
    ModelDelegate delegate;
    std::unique_ptr<ui::SimpleMenuModel> model;
    std::unique_ptr<views::MenuRunner> runner;
};

void Menu::Show(const std::vector<MenuItem>& items,
                Widget* anchor,
                int x,
                int y) {
    CHECK(anchor != nullptr);

    auto* impl = new Impl;
    for (const auto& item : items) {
        if (item.text == nullptr) {
            impl->entries.push_back(MenuEntry{});
            continue;
        }
        impl->entries.push_back(MenuEntry{
            std::u16string(item.text),
            item.on_click,  // 拷贝:入参来自调用方堆。
            item.enabled,
            item.checked,
        });
    }
    impl->delegate.entries = &impl->entries;

    impl->model = std::make_unique<ui::SimpleMenuModel>(&impl->delegate);
    for (size_t i = 0; i < impl->entries.size(); ++i) {
        const auto& entry = impl->entries[i];
        if (entry.text.empty() && !entry.on_click) {
            impl->model->AddSeparator(ui::NORMAL_SEPARATOR);
            continue;
        }
        if (entry.checked)
            impl->model->AddCheckItem(static_cast<int>(i), entry.text);
        else
            impl->model->AddItem(static_cast<int>(i), entry.text);
    }

    views::View* anchor_native = internal::WidgetBridge::GetNative(anchor);
    gfx::Rect bounds = anchor_native->GetBoundsInScreen();
    bounds.set_origin(bounds.origin() + gfx::Vector2d(x, y));

    // 关闭后异步自删:回调内同步销毁 MenuRunner 会重入其关闭路径。
    impl->runner = std::make_unique<views::MenuRunner>(
        impl->model.get(), views::MenuRunner::CONTEXT_MENU,
        base::BindRepeating([](Impl* self) {
            base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE,
                base::BindOnce([](Impl* self) { delete self; }, self));
        }, impl));
    impl->runner->RunMenuAt(anchor_native->GetWidget(), nullptr, bounds,
                            views::MenuAnchorPosition::kTopLeft,
                            ui::mojom::MenuSourceType::kMouse);
}

}  // namespace liew
