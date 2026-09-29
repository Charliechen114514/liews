#pragma once

namespace liew {

struct Insets {
    constexpr Insets() = default;
    constexpr Insets(int left, int top, int right, int bottom)
        : left_(left), top_(top), right_(right), bottom_(bottom) {}

    constexpr auto left() const noexcept { return left_; }
    constexpr auto top() const noexcept { return top_; }
    constexpr auto right() const noexcept { return right_; }
    constexpr auto bottom() const noexcept { return bottom_; }

    void set_left(int left) noexcept { left_ = left; }
    void set_top(int top) noexcept { top_ = top; }
    void set_right(int right) noexcept { right_ = right; }
    void set_bottom(int bottom) noexcept { bottom_ = bottom; }

  private:
    int left_ = 0;
    int top_ = 0;
    int right_ = 0;
    int bottom_ = 0;
};

}  // namespace liew
