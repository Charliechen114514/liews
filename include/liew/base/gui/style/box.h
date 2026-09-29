#pragma once
#include "liew/base/gui/color.h"
#include "liew/base/insets.h"
#include <optional>

namespace liew::style {

struct Box {
    std::optional<Color> background;
    std::optional<float> corner_radius;
    std::optional<Insets> padding;
};

} // namespace liew::style