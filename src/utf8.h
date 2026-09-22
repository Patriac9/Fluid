#pragma once
#include <cstdint>
#include <string_view>

namespace Fluid::detail {

// Consume one Unicode scalar; malformed input advances safely and draws a fallback.
inline std::uint32_t decode_utf8(std::string_view text, std::size_t &cursor) {
    if (cursor >= text.size())
        return 0;
    const auto first = static_cast<unsigned char>(text[cursor++]);
    if (first < 0x80)
        return first;
    int continuation = 0;
    std::uint32_t scalar = 0;
    std::uint32_t minimum = 0;
    if ((first & 0xE0) == 0xC0) {
        continuation = 1;
        scalar = first & 0x1F;
        minimum = 0x80;
    } else if ((first & 0xF0) == 0xE0) {
        continuation = 2;
        scalar = first & 0x0F;
        minimum = 0x800;
    } else if ((first & 0xF8) == 0xF0) {
        continuation = 3;
        scalar = first & 7;
        minimum = 0x10000;
    } else
        return 0xFFFD;
    for (int i = 0; i < continuation; ++i) {
        if (cursor >= text.size())
            return 0xFFFD;
        const auto byte = static_cast<unsigned char>(text[cursor]);
        if ((byte & 0xC0) != 0x80)
            return 0xFFFD;
        scalar = (scalar << 6) | (byte & 0x3F);
        ++cursor;
    }
    if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF))
        return 0xFFFD;
    return scalar;
}

inline std::size_t previous_utf8(std::string_view text, std::size_t cursor) {
    if (!cursor)
        return 0;
    --cursor;
    while (cursor && (static_cast<unsigned char>(text[cursor]) & 0xC0) == 0x80)
        --cursor;
    return cursor;
}

inline std::size_t next_utf8(std::string_view text, std::size_t cursor) {
    decode_utf8(text, cursor);
    return cursor;
}

} // namespace Fluid::detail
