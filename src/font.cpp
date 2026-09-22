#include "fluid/ui.h"
#include "utf8.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_PRIVATE
#define NK_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "nuklear.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Fluid {

struct FontAtlas::Impl {
    nk_font_atlas atlas{};
    nk_font *regular = nullptr;
    nk_font *bold = nullptr;
    std::vector<unsigned char> rgba;
    int width = 0;
    int height = 0;
    Vec2 white{};
    float size = 36.0f;
    float regular_em_scale = 1.0f;
    float bold_em_scale = 1.0f;
    Impl() { nk_font_atlas_init_default(&atlas); }
    ~Impl() { nk_font_atlas_clear(&atlas); }
};

FontAtlas::FontAtlas() : impl_(std::make_unique<Impl>()) {
    auto &data = *impl_;
    nk_font_atlas_begin(&data.atlas);
    // Read installed fonts without distributing platform-owned font files.
    std::string windows = "C:/Windows";
    if (const char *root = std::getenv("WINDIR"))
        windows = root;
    const std::vector<std::pair<std::string, std::string>> candidates = {
        {windows + "/Fonts/segoeui.ttf", windows + "/Fonts/seguisb.ttf"},
        {"/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf"},
        {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"},
        {"/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
         "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf"},
        {"/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"}};
    static const nk_rune ranges[] = {0x20, 0x24F, 0x370, 0x52F, 0x2000, 0x206F, 0x2190, 0x21FF, 0};
    auto config = nk_font_config(data.size);
    config.oversample_h = 2;
    config.oversample_v = 2;
    config.pixel_snap = 0;
    config.range = ranges;
    config.fallback_glyph = '?';
    for (const auto &candidate : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(candidate.first, error))
            continue;
        data.regular = nk_font_atlas_add_from_file(&data.atlas, candidate.first.c_str(), data.size, &config);
        if (!data.regular)
            continue;
        if (std::filesystem::is_regular_file(candidate.second, error))
            data.bold =
                nk_font_atlas_add_from_file(&data.atlas, candidate.second.c_str(), data.size, &config);
        break;
    }
    if (!data.regular) {
        config.range = nk_font_default_glyph_ranges();
        data.regular = nk_font_atlas_add_default(&data.atlas, data.size, &config);
    }
    if (!data.bold)
        data.bold = data.regular;
    if (!data.regular)
        throw std::runtime_error("Fluid could not initialize its font atlas.");
    const auto *pixels = static_cast<const unsigned char *>(
        nk_font_atlas_bake(&data.atlas, &data.width, &data.height, NK_FONT_ATLAS_RGBA32));
    if (!pixels || data.width <= 0 || data.height <= 0)
        throw std::runtime_error("Fluid could not bake its font atlas.");
    // Nuklear bakes ascent-to-descent pixels. Public sizes use em pixels so a
    // 14-pixel label has the same readable proportions as desktop/CSS typography.
    const auto em_scale = [&](nk_font *font) {
        nk_tt_fontinfo face{};
        if (!font->config || !font->config->ttf_blob ||
            !nk_tt_InitFont(&face, static_cast<const unsigned char *>(font->config->ttf_blob), 0))
            return 1.0f;
        return nk_tt_ScaleForMappingEmToPixels(&face, data.size) /
               nk_tt_ScaleForPixelHeight(&face, data.size);
    };
    data.regular_em_scale = em_scale(data.regular);
    data.bold_em_scale = em_scale(data.bold);
    data.rgba.assign(pixels, pixels + static_cast<std::size_t>(data.width) * data.height * 4);
    nk_draw_null_texture white{};
    nk_font_atlas_end(&data.atlas, nk_handle_id(1), &white);
    data.white = {white.uv.x, white.uv.y};
    nk_font_atlas_cleanup(&data.atlas);
}

FontAtlas::~FontAtlas() = default;
const std::vector<unsigned char> &FontAtlas::pixels() const { return impl_->rgba; }
int FontAtlas::width() const { return impl_->width; }
int FontAtlas::height() const { return impl_->height; }
Vec2 FontAtlas::white_uv() const { return impl_->white; }
float FontAtlas::base_size() const { return impl_->size; }

FontAtlas::Glyph FontAtlas::glyph(std::uint32_t codepoint, bool bold) const {
    nk_font *font = bold ? impl_->bold : impl_->regular;
    const nk_font_glyph *glyph = nk_font_find_glyph(font, codepoint);
    if (!glyph)
        return {};
    const float scale = bold ? impl_->bold_em_scale : impl_->regular_em_scale;
    const float origin = font->info.ascent + 0.5f;
    const float baseline = impl_->size * 0.8f;
    return {glyph->x0 * scale,
            (glyph->y0 - origin) * scale + baseline,
            glyph->x1 * scale,
            (glyph->y1 - origin) * scale + baseline,
            glyph->u0,
            glyph->v0,
            glyph->u1,
            glyph->v1,
            glyph->xadvance * scale};
}

float FontAtlas::measure(std::string_view text, float size, bool bold) const {
    float line = 0;
    float maximum = 0;
    const float scale = std::max(0.0f, size) / base_size();
    for (std::size_t cursor = 0; cursor < text.size();) {
        const auto scalar = detail::decode_utf8(text, cursor);
        if (scalar == '\n') {
            maximum = std::max(maximum, line);
            line = 0;
        } else if (scalar == '\t')
            line += glyph(' ', bold).advance * scale * 4;
        else if (scalar != '\r')
            line += glyph(scalar, bold).advance * scale;
    }
    return std::max(line, maximum);
}

} // namespace Fluid
