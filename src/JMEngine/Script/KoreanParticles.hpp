#pragma once

#include <string>

namespace jm {

enum class KoreanParticle {
    Subject,
    Object,
    Topic,
    With,
    Direction,
};

std::string attachKoreanParticle(const std::string& noun, KoreanParticle particle);

} // namespace jm
