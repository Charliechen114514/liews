/* Need better style? :)  */
#pragma once

#include "liew/base/gui/style/box.h"
#include "liew/base/gui/style/text.h"

namespace liew {

// Button Style is composed of text style and box style
struct ButtonStyle {
    style::Text text_style;
    style::Box box_style;
    std::optional<int> image_label_spacing;
};

} // namespace liew
