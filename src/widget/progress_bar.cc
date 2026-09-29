#include "liew/widget/widgets/progress_bar.h"
#include "src/widget/widget_impl.h"
#include "ui/views/controls/progress_bar.h"

namespace liew {

class ProgressBar::Impl : public Widget::Impl {
  public:
    views::ProgressBar* bar() {
        return static_cast<views::ProgressBar*>(native);
    }
};

ProgressBar::ProgressBar() {
    impl_ = new ProgressBar::Impl;
    impl_->native = new views::ProgressBar;
}

ProgressBar::~ProgressBar() = default;

double ProgressBar::GetValue() const {
    return static_cast<ProgressBar::Impl*>(impl_)->bar()->GetValue();
}

void ProgressBar::SetValue(double value) {
    static_cast<ProgressBar::Impl*>(impl_)->bar()->SetValue(value);
}

}  // namespace liew
