#pragma once

#include <cstdint>

namespace liew {
struct Size {
    constexpr Size() = default;
    constexpr Size(uint32_t width, uint32_t height) : width_(width), height_(height) {}

    constexpr auto width() noexcept { return width_; }
    constexpr auto height() noexcept { return height_; }

    void setWidth(uint32_t width) noexcept { width_ = width; }
    void setHeight(uint32_t height) noexcept { height_ = height; }

  private:
    uint32_t width_{};
    uint32_t height_{};
};

} // namespace liew