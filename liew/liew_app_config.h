#pragma once

#include <cstdint>

namespace liew {

struct AppConfig {
    // Used as the default title for windows whose title is empty.
    const char16_t* application_name = u"liew";
};

struct WindowConfig {
    // The string is copied before CreateWindow() returns and may be temporary.
    // nullptr or an empty string selects AppConfig::application_name.
    const char16_t* title = nullptr;
    int width = 800;
    int height = 600;
    bool resizable = true;
    bool center = true;
};

using WindowId = std::uint64_t;
inline constexpr WindowId kInvalidWindowId = 0;

} // namespace liew
