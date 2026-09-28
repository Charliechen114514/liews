#pragma once

#include "liew/base/size.h"

#include <cstdint>
#include <string_view>
#include <utility>

namespace liew {

struct AppConfig {
    AppConfig(int argc, const char** const argv) : argument_count_(argc), argument_params_(argv) {}
    // Used as the default title for windows whose title is empty.
    const char16_t* application_name = u"liew";
    // Resource pak file name, resolved against the exe directory (DIR_ASSETS).
    // The pointed-to string only needs to outlive Application's construction.
    std::string_view resource_pak_name = "liew_resources.pak";
    const int argument_count_;
    const char** const argument_params_;

  private:
    AppConfig() = delete;
};

struct WindowProperty {
    bool isResizable{true};
    bool isCenter{true};
};

struct WindowConfig {
    const char16_t* title = nullptr;
    Size size{800, 600};
    WindowProperty property;
};

using WindowId = std::uint64_t;
inline constexpr WindowId kInvalidWindowId = 0;

} // namespace liew
