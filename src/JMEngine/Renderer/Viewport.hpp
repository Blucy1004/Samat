#pragma once

#include <algorithm>
#include <cmath>

namespace jm {

struct PixelViewport {
    int x{0};
    int yFromTop{0};
    int width{0};
    int height{0};
};

inline PixelViewport scaleViewportToPixels(float x, float y, float width, float height,
                                           float logicalWidth, float logicalHeight,
                                           int framebufferWidth, int framebufferHeight) {
    if (logicalWidth <= 0.0F || logicalHeight <= 0.0F || framebufferWidth <= 0 || framebufferHeight <= 0) return {};
    const float scaleX = static_cast<float>(framebufferWidth) / logicalWidth;
    const float scaleY = static_cast<float>(framebufferHeight) / logicalHeight;
    const int left = std::clamp(static_cast<int>(std::lround(x * scaleX)), 0, framebufferWidth);
    const int top = std::clamp(static_cast<int>(std::lround(y * scaleY)), 0, framebufferHeight);
    const int right = std::clamp(static_cast<int>(std::lround((x + width) * scaleX)), left, framebufferWidth);
    const int bottom = std::clamp(static_cast<int>(std::lround((y + height) * scaleY)), top, framebufferHeight);
    return {left, top, right - left, bottom - top};
}

inline int viewportBottomLeftY(const PixelViewport& viewport, int framebufferHeight) {
    return framebufferHeight - viewport.yFromTop - viewport.height;
}

} // namespace jm
