#pragma once

#include "liew/base/gui/style/box.h"
#include "liew/liew_export.h"
#include "liew/widget/basic/widget.h"

namespace liew {

class LIEW_API Container : public Widget {
  public:
    Container();
    ~Container() override;

    void setStyle(style::Box style);
};

} // namespace liew
