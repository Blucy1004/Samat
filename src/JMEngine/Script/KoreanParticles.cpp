#include "JMEngine/Script/KoreanParticles.hpp"

#include <cctype>

namespace jm {
namespace {

unsigned int lastCodepoint(const std::string& text) {
    std::size_t end = text.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    if (end == 0) return 0;
    std::size_t start = end - 1;
    while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0U) == 0x80U) --start;
    const unsigned char first = static_cast<unsigned char>(text[start]);
    if ((first & 0x80U) == 0) return first;
    if ((first & 0xE0U) == 0xC0U && start + 1 < end) {
        return ((first & 0x1FU) << 6) | (static_cast<unsigned char>(text[start + 1]) & 0x3FU);
    }
    if ((first & 0xF0U) == 0xE0U && start + 2 < end) {
        return ((first & 0x0FU) << 12) |
               ((static_cast<unsigned char>(text[start + 1]) & 0x3FU) << 6) |
               (static_cast<unsigned char>(text[start + 2]) & 0x3FU);
    }
    if ((first & 0xF8U) == 0xF0U && start + 3 < end) {
        return ((first & 0x07U) << 18) |
               ((static_cast<unsigned char>(text[start + 1]) & 0x3FU) << 12) |
               ((static_cast<unsigned char>(text[start + 2]) & 0x3FU) << 6) |
               (static_cast<unsigned char>(text[start + 3]) & 0x3FU);
    }
    return first;
}

} // namespace

std::string attachKoreanParticle(const std::string& noun, KoreanParticle particle) {
    const unsigned int codepoint = lastCodepoint(noun);
    const bool hangulSyllable = codepoint >= 0xAC00U && codepoint <= 0xD7A3U;
    const unsigned int jongseong = hangulSyllable ? (codepoint - 0xAC00U) % 28U : 0U;
    const bool hasFinal = jongseong != 0U;
    const bool finalRieul = jongseong == 8U;

    const char* suffix = "";
    switch (particle) {
    case KoreanParticle::Subject: suffix = hasFinal ? "이" : "가"; break;
    case KoreanParticle::Object: suffix = hasFinal ? "을" : "를"; break;
    case KoreanParticle::Topic: suffix = hasFinal ? "은" : "는"; break;
    case KoreanParticle::With: suffix = hasFinal ? "과" : "와"; break;
    case KoreanParticle::Direction: suffix = (!hasFinal || finalRieul) ? "로" : "으로"; break;
    }
    return noun + suffix;
}

} // namespace jm
