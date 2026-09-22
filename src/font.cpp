#include "fluid/ui.h"
#include "utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#endif

namespace Fluid {
namespace {
constexpr int kMaxImageEdge = 4096;
constexpr int kAtlasWidth = 1024;
constexpr int kAtlasHeight = 2048;
constexpr int kMaxAtlasEdge = 8192;
constexpr int kMaxPixelSize = 256;

std::filesystem::path utf8_path(const std::string &text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
}

std::vector<unsigned char> read_file(const std::string &path) {
    const auto file = utf8_path(path);
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error))
        throw std::runtime_error("Font file was not found: " + path);
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open font file: " + path);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.empty())
        throw std::runtime_error("Font file is empty: " + path);
    return bytes;
}

std::vector<unsigned char> scale_rgba(const unsigned char *src, int sw, int sh, int dw, int dh) {
    std::vector<unsigned char> dst(static_cast<std::size_t>(dw) * dh * 4);
    for (int y = 0; y < dh; ++y) {
        const float gy = (static_cast<float>(y) + 0.5f) * sh / dh - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, sh - 1);
        const int y1 = std::min(y0 + 1, sh - 1);
        const float ty = gy - std::floor(gy);
        for (int x = 0; x < dw; ++x) {
            const float gx = (static_cast<float>(x) + 0.5f) * sw / dw - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, sw - 1);
            const int x1 = std::min(x0 + 1, sw - 1);
            const float tx = gx - std::floor(gx);
            unsigned char *out = dst.data() + (static_cast<std::size_t>(y) * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                const float p00 = src[(static_cast<std::size_t>(y0) * sw + x0) * 4 + c];
                const float p10 = src[(static_cast<std::size_t>(y0) * sw + x1) * 4 + c];
                const float p01 = src[(static_cast<std::size_t>(y1) * sw + x0) * 4 + c];
                const float p11 = src[(static_cast<std::size_t>(y1) * sw + x1) * 4 + c];
                const float value = (p00 * (1 - tx) + p10 * tx) * (1 - ty) + (p01 * (1 - tx) + p11 * tx) * ty;
                out[c] = static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f));
            }
        }
    }
    return dst;
}

#if defined(_WIN32)
std::wstring wide_path(const std::string &utf8) {
    if (utf8.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                          static_cast<int>(utf8.size()), nullptr, 0);
    if (count <= 0)
        throw std::runtime_error("Image path is not valid UTF-8: " + utf8);
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                        count);
    return wide;
}

struct ComApartment {
    bool release = false;
    ComApartment() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        release = hr == S_OK;
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            throw std::runtime_error("Cannot initialize image loading");
    }
    ~ComApartment() {
        if (release)
            CoUninitialize();
    }
};

std::vector<unsigned char> decode_image_file(const std::string &path, int &width, int &height) {
    ComApartment apartment;
    IWICImagingFactory *factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        throw std::runtime_error("Cannot create the image decoder");
    IWICBitmapDecoder *decoder = nullptr;
    const auto wide = wide_path(path);
    HRESULT hr = factory->CreateDecoderFromFilename(wide.c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        factory->Release();
        throw std::runtime_error("Cannot open image: " + path);
    }
    IWICBitmapFrameDecode *frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    IWICFormatConverter *converter = nullptr;
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr))
        hr = converter->GetSize(&w, &h);
    std::vector<unsigned char> pixels;
    if (SUCCEEDED(hr) && w > 0 && h > 0 && w <= kMaxImageEdge && h <= kMaxImageEdge) {
        pixels.resize(static_cast<std::size_t>(w) * h * 4);
        hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data());
    } else if (SUCCEEDED(hr))
        hr = E_FAIL;
    if (converter)
        converter->Release();
    if (frame)
        frame->Release();
    decoder->Release();
    factory->Release();
    if (FAILED(hr) || pixels.empty())
        throw std::runtime_error("Cannot decode image: " + path);
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return pixels;
}
#else
std::vector<unsigned char> decode_image_file(const std::string &, int &, int &) {
    throw std::runtime_error("Image files can be loaded on Windows. Pass RGBA pixels to add_image on this platform.");
}
#endif

void *glyph_alloc(nk_handle, void *, nk_size size) { return std::malloc(size); }
void glyph_free(nk_handle, void *pointer) { std::free(pointer); }

bool font_bytes_valid(const std::vector<unsigned char> &bytes) {
    if (bytes.size() < 12)
        return false;
    nk_tt_fontinfo info{};
    return nk_tt_InitFont(&info, bytes.data(), 0) != 0;
}

std::vector<unsigned char> default_ttf() {
    const int compressed_size =
        ((static_cast<int>(std::strlen(nk_proggy_clean_ttf_compressed_data_base85)) + 4) / 5) * 4;
    std::vector<unsigned char> compressed(static_cast<std::size_t>(compressed_size));
    nk_decode_85(compressed.data(),
                 reinterpret_cast<const unsigned char *>(nk_proggy_clean_ttf_compressed_data_base85));
    const unsigned int size = nk_decompress_length(compressed.data());
    if (size == 0)
        return {};
    std::vector<unsigned char> ttf(size);
    if (nk_decompress(ttf.data(), compressed.data(), static_cast<unsigned int>(compressed_size)) == 0)
        return {};
    return ttf;
}

std::vector<unsigned char> read_if_present(const std::string &path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(utf8_path(path), error))
        return {};
    return read_file(path);
}
} // namespace

struct FontAtlas::Impl {
    struct Typeface {
        std::vector<unsigned char> bytes;
        nk_tt_fontinfo info{};
        bool valid = false;
    };
    struct StoredImage {
        std::vector<unsigned char> rgba;
        int width = 0;
        int height = 0;
        int x = 0, y = 0, w = 0, h = 0;
    };
    struct CachedGlyph {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float advance = 0;
        int x = 0, y = 0, w = 0, h = 0;
    };

    Typeface regular;
    Typeface bold;
    std::vector<StoredImage> images;
    std::string regular_path;
    std::string bold_path;
    mutable std::vector<unsigned char> rgba;
    mutable std::unordered_map<std::uint64_t, CachedGlyph> cache;
    mutable int width = 0;
    mutable int height = 0;
    mutable int pen_x = 0;
    mutable int pen_y = 0;
    mutable int row_h = 0;
    mutable std::uint64_t revision = 0;

    bool init_face(Typeface &face) {
        face.valid = false;
        if (face.bytes.size() < 12)
            return false;
        std::memset(&face.info, 0, sizeof(face.info));
        face.valid = nk_tt_InitFont(&face.info, face.bytes.data(), 0) != 0;
        return face.valid;
    }

    void load_faces() {
        if (regular.bytes.empty()) {
            std::string windows = "C:/Windows";
            if (const char *root = std::getenv("WINDIR"))
                windows = root;
            const std::vector<std::pair<std::string, std::string>> candidates = {
                {windows + "/Fonts/segoeui.ttf", windows + "/Fonts/seguisb.ttf"},
                {"/System/Library/Fonts/Supplemental/Arial.ttf",
                 "/System/Library/Fonts/Supplemental/Arial Bold.ttf"},
                {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                 "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"},
                {"/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
                 "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf"},
                {"/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"}};
            for (const auto &candidate : candidates) {
                regular.bytes = read_if_present(candidate.first);
                if (regular.bytes.empty())
                    continue;
                if (bold.bytes.empty())
                    bold.bytes = read_if_present(candidate.second);
                break;
            }
        }
        if (regular.bytes.empty())
            regular.bytes = default_ttf();
        if (!init_face(regular))
            throw std::runtime_error(regular_path.empty() ? "Fluid could not initialize its font atlas."
                                                         : "Cannot bake font: " + regular_path);
        if (!bold.bytes.empty() && !init_face(bold)) {
            bold.bytes.clear();
            bold.valid = false;
        }
    }

    void grow(int needed_bottom) const {
        while (needed_bottom > height) {
            if (height >= kMaxAtlasEdge)
                throw std::runtime_error("UI atlas is larger than 8192 pixels");
            const int next = std::min(height * 2, kMaxAtlasEdge);
            rgba.resize(static_cast<std::size_t>(width) * next * 4, 0);
            height = next;
        }
    }

    void reserve(int w, int h, int &x, int &y) const {
        constexpr int gap = 1;
        if (w + gap > width)
            throw std::runtime_error("Glyph is wider than the font atlas");
        if (pen_x + w + gap > width) {
            pen_y += row_h;
            pen_x = 0;
            row_h = 0;
        }
        grow(pen_y + h + gap);
        x = pen_x;
        y = pen_y;
        pen_x += w + gap;
        row_h = std::max(row_h, h + gap);
    }

    void blit_rgba(int x, int y, const unsigned char *pixels, int w, int h) const {
        for (int row = 0; row < h; ++row) {
            std::memcpy(rgba.data() + (static_cast<std::size_t>(y + row) * width + x) * 4,
                        pixels + static_cast<std::size_t>(row) * w * 4, static_cast<std::size_t>(w) * 4);
        }
    }

    void blit_coverage(int x, int y, const unsigned char *alpha, int w, int h) const {
        for (int row = 0; row < h; ++row) {
            unsigned char *destination = rgba.data() + (static_cast<std::size_t>(y + row) * width + x) * 4;
            const unsigned char *source = alpha + static_cast<std::size_t>(row) * w;
            for (int col = 0; col < w; ++col) {
                destination[col * 4 + 0] = 255;
                destination[col * 4 + 1] = 255;
                destination[col * 4 + 2] = 255;
                destination[col * 4 + 3] = source[col];
            }
        }
    }

    void place_image(StoredImage &image) const {
        int placed_w = image.width;
        int placed_h = image.height;
        const unsigned char *pixels = image.rgba.data();
        std::vector<unsigned char> scaled;
        if (placed_w > width - 2) {
            const float fit = static_cast<float>(width - 2) / placed_w;
            const int dw = width - 2;
            const int dh = std::max(1, static_cast<int>(std::lround(placed_h * fit)));
            scaled = scale_rgba(pixels, placed_w, placed_h, dw, dh);
            pixels = scaled.data();
            placed_w = dw;
            placed_h = dh;
        }
        reserve(placed_w, placed_h, image.x, image.y);
        blit_rgba(image.x, image.y, pixels, placed_w, placed_h);
        image.w = placed_w;
        image.h = placed_h;
    }

    void reset_atlas() {
        cache.clear();
        width = kAtlasWidth;
        height = kAtlasHeight;
        rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = 255;
        pen_x = 2;
        pen_y = 0;
        row_h = 1;
        for (auto &image : images)
            place_image(image);
        ++revision;
    }

    CachedGlyph cache_glyph(std::uint32_t codepoint, bool bold_text, int pixel) const {
        const bool use_bold = bold_text && bold.valid;
        const std::uint64_t key = (static_cast<std::uint64_t>(pixel) << 32) | (use_bold ? 0x80000000ull : 0ull) |
                                  codepoint;
        if (const auto found = cache.find(key); found != cache.end())
            return found->second;
        const Typeface &face = use_bold ? bold : regular;
        const float scale = nk_tt_ScaleForMappingEmToPixels(&face.info, static_cast<float>(pixel));
        int index = nk_tt_FindGlyphIndex(&face.info, static_cast<int>(codepoint));
        if (index == 0)
            index = nk_tt_FindGlyphIndex(&face.info, '?');
        int advance_units = 0;
        int left_bearing = 0;
        nk_tt_GetGlyphHMetrics(&face.info, index, &advance_units, &left_bearing);
        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
        nk_tt_GetGlyphBitmapBox(&face.info, index, scale, scale, &ix0, &iy0, &ix1, &iy1);
        CachedGlyph shape;
        shape.advance = advance_units * scale;
        const float baseline = static_cast<float>(pixel) * 0.8f;
        shape.x0 = static_cast<float>(ix0);
        shape.y0 = static_cast<float>(iy0) + baseline;
        shape.x1 = static_cast<float>(ix1);
        shape.y1 = static_cast<float>(iy1) + baseline;
        const int glyph_w = std::max(0, ix1 - ix0);
        const int glyph_h = std::max(0, iy1 - iy0);
        if (glyph_w > 0 && glyph_h > 0 && glyph_w + 1 <= width) {
            std::vector<unsigned char> alpha(static_cast<std::size_t>(glyph_w) * glyph_h, 0);
            nk_allocator allocator{};
            allocator.alloc = glyph_alloc;
            allocator.free = glyph_free;
            nk_tt_MakeGlyphBitmapSubpixel(&face.info, alpha.data(), glyph_w, glyph_h, glyph_w, scale, scale, 0, 0,
                                          index, &allocator);
            reserve(glyph_w, glyph_h, shape.x, shape.y);
            blit_coverage(shape.x, shape.y, alpha.data(), glyph_w, glyph_h);
            shape.w = glyph_w;
            shape.h = glyph_h;
            ++revision;
        }
        return cache.emplace(key, shape).first->second;
    }

    FontAtlas::Glyph glyph(std::uint32_t codepoint, bool bold_text, float size) const {
        if (!regular.valid || size <= 0)
            return {};
        const int pixel = std::clamp(static_cast<int>(std::lround(size)), 1, kMaxPixelSize);
        const CachedGlyph shape = cache_glyph(codepoint, bold_text, pixel);
        const float fit = size / static_cast<float>(pixel);
        FontAtlas::Glyph result;
        result.x0 = shape.x0 * fit;
        result.y0 = shape.y0 * fit;
        result.x1 = shape.x1 * fit;
        result.y1 = shape.y1 * fit;
        result.advance = shape.advance * fit;
        if (shape.w > 0 && shape.h > 0 && width > 0 && height > 0) {
            result.u0 = static_cast<float>(shape.x) / static_cast<float>(width);
            result.v0 = static_cast<float>(shape.y) / static_cast<float>(height);
            result.u1 = static_cast<float>(shape.x + shape.w) / static_cast<float>(width);
            result.v1 = static_cast<float>(shape.y + shape.h) / static_cast<float>(height);
        }
        return result;
    }
};

FontAtlas::FontAtlas() : impl_(std::make_unique<Impl>()) {
    impl_->load_faces();
    impl_->reset_atlas();
}
FontAtlas::~FontAtlas() = default;

void FontAtlas::set_typeface(std::string regular_path, std::string bold_path) {
    std::vector<unsigned char> regular_bytes;
    std::vector<unsigned char> bold_bytes;
    if (!regular_path.empty()) {
        regular_bytes = read_file(regular_path);
        if (!font_bytes_valid(regular_bytes))
            throw std::runtime_error("Cannot bake font: " + regular_path);
    }
    if (!bold_path.empty()) {
        bold_bytes = read_file(bold_path);
        if (!font_bytes_valid(bold_bytes))
            throw std::runtime_error("Cannot bake font: " + bold_path);
    }
    impl_->regular_path = std::move(regular_path);
    impl_->bold_path = std::move(bold_path);
    impl_->regular = {};
    impl_->bold = {};
    impl_->regular.bytes = std::move(regular_bytes);
    impl_->bold.bytes = std::move(bold_bytes);
    impl_->load_faces();
    impl_->reset_atlas();
}

Image FontAtlas::add_image(const std::uint8_t *rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0 || width > kMaxImageEdge || height > kMaxImageEdge)
        throw std::invalid_argument("Image must be a positive RGBA buffer no larger than 4096 pixels");
    Impl::StoredImage image;
    image.width = width;
    image.height = height;
    image.rgba.assign(rgba, rgba + static_cast<std::size_t>(width) * height * 4);
    impl_->images.push_back(std::move(image));
    impl_->place_image(impl_->images.back());
    ++impl_->revision;
    return Image{static_cast<int>(impl_->images.size()) - 1};
}

Image FontAtlas::add_image_file(const std::string &path) {
    int width = 0, height = 0;
    const auto pixels = decode_image_file(path, width, height);
    return add_image(pixels.data(), width, height);
}

const std::vector<unsigned char> &FontAtlas::pixels() const { return impl_->rgba; }
int FontAtlas::width() const { return impl_->width; }
int FontAtlas::height() const { return impl_->height; }
Vec2 FontAtlas::white_uv() const {
    if (impl_->width <= 0 || impl_->height <= 0)
        return {};
    return {0.5f / static_cast<float>(impl_->width), 0.5f / static_cast<float>(impl_->height)};
}
std::uint64_t FontAtlas::revision() const { return impl_->revision; }

bool FontAtlas::image_uv(Image image, Vec2 &uv0, Vec2 &uv1) const {
    if (image.id < 0 || static_cast<std::size_t>(image.id) >= impl_->images.size() || impl_->width <= 0 ||
        impl_->height <= 0)
        return false;
    const auto &stored = impl_->images[static_cast<std::size_t>(image.id)];
    if (stored.w <= 0 || stored.h <= 0)
        return false;
    uv0 = {static_cast<float>(stored.x) / static_cast<float>(impl_->width),
           static_cast<float>(stored.y) / static_cast<float>(impl_->height)};
    uv1 = {static_cast<float>(stored.x + stored.w) / static_cast<float>(impl_->width),
           static_cast<float>(stored.y + stored.h) / static_cast<float>(impl_->height)};
    return true;
}

FontAtlas::Glyph FontAtlas::glyph(std::uint32_t codepoint, bool bold, float size) const {
    return impl_->glyph(codepoint, bold, size);
}

float FontAtlas::measure(std::string_view text, float size, bool bold) const { return ink(text, size, bold).width; }

TextInk FontAtlas::ink(std::string_view text, float size, bool bold) const {
    TextInk result;
    if (size <= 0)
        return result;
    bool any = false;
    float line = 0, top = 0, bottom = 0;
    for (std::size_t cursor = 0; cursor < text.size();) {
        const auto scalar = detail::decode_utf8(text, cursor);
        if (scalar == '\n') {
            result.width = std::max(result.width, line);
            line = 0;
            continue;
        }
        if (scalar == '\r')
            continue;
        if (scalar == '\t') {
            line += glyph(' ', bold, size).advance * 4;
            continue;
        }
        const auto shape = glyph(scalar, bold, size);
        line += shape.advance;
        if (shape.y1 > shape.y0) {
            top = any ? std::min(top, shape.y0) : shape.y0;
            bottom = any ? std::max(bottom, shape.y1) : shape.y1;
            any = true;
        }
    }
    result.width = std::max(result.width, line);
    if (!any) {
        top = size * 0.2f;
        bottom = size * 0.8f;
    }
    result.top = top;
    result.bottom = bottom;
    return result;
}

} // namespace Fluid
