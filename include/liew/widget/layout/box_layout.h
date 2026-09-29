#pragma once

#include "liew/base/insets.h"

namespace liew {

enum class Orientation {
    Horizontal,
    Vertical,
};

struct BoxLayout {
    Orientation orientation = Orientation::Horizontal;
    int spacing = 0;
    Insets padding;
};

}  // namespace liew
