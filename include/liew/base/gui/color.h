#pragma once
#include <cstdint>

namespace liew {
struct Color {

    constexpr Color(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
        : r(r), g(g), b(b), a(a) {}
    constexpr Color() : Color(0, 0, 0, 0) {}
    constexpr Color(std::uint8_t r, std::uint8_t g, std::uint8_t b) : Color(r, g, b, 255) {}

    constexpr static Color Rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return Color(r, g, b);
    }

    constexpr static Color Rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
        return Color(r, g, b, a);
    }

    std::uint8_t r, g, b, a;
};
} // namespace liew
