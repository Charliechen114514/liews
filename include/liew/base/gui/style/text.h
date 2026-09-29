#pragma once
#include "liew/base/gui/color.h"
#include <optional>

namespace liew::style {

struct Text {
    std::optional<liew::Color> color; // text color
    std::optional<int> size;          // font size
    std::optional<int> weight;        // font weight
    bool italic = false;              // is font italic
    bool underline = false;           // is font underline
    bool strikeout = false;           // is font strikeout
    bool bold = false;                // is font bold
};

} // namespace liew::style
