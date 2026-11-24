// SPDX-License-Identifier: GPL-3.0-or-later
//
// Icon packs (icon_packs.hpp).
#include "icon_packs.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <tuple>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../third_party/stb_image.h"
#define NANOSVG_IMPLEMENTATION
#include "../third_party/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../third_party/nanosvgrast.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

namespace spiral {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool read_file(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

long long write_time(const fs::path& p) {
    std::error_code e;
    const auto t = fs::last_write_time(p, e);
    return e ? 0 : static_cast<long long>(t.time_since_epoch().count());
}

// A path inside the pack's folder: relative, no "..".
bool inside(const std::string& relative) {
    const fs::path p(relative);
    if (relative.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory()) {
        return false;
    }
    for (const fs::path& part : p) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

bool glyph_file(const std::string& name) {
    const std::string l = lower(name);
    return l.size() > 4 && (l.compare(l.size() - 4, 4, ".svg") == 0 || l.compare(l.size() - 4, 4, ".png") == 0);
}

// The picture cropped to where its alpha is, a pixel of room around; white.
bool crop(const unsigned char* rgba, int w, int h, Picture& out) {
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4 + 3] > 0) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        }
    }
    if (x1 < x0) {
        return false;
    }
    x0 = std::max(0, x0 - 1);
    y0 = std::max(0, y0 - 1);
    x1 = std::min(w - 1, x1 + 1);
    y1 = std::min(h - 1, y1 + 1);
    out.width = x1 - x0 + 1;
    out.height = y1 - y0 + 1;
    out.rgba.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 255);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)) * 4 + 3] =
                rgba[(static_cast<std::size_t>(y + y0) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x + x0)) * 4 + 3];
        }
    }
    return true;
}

}  // namespace

bool parse_item(const std::string& text, int& item) {
    std::string t = lower(text);
    if (t.rfind("0x", 0) == 0) {
        t = t.substr(2);
    }
    if (t.empty() || t.size() > 4) {
        return false;
    }
    int v = 0;
    for (const char c : t) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        v = v * 16 + (std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : c - 'a' + 10);
    }
    item = v;
    return true;
}

bool parse_colour(const std::string& text, look::Rgb& out) {
    if (text.size() != 7 || text[0] != '#') {
        return false;
    }
    int v[3];
    for (int k = 0; k < 3; ++k) {
        int item = 0;
        if (!parse_item(text.substr(1 + 2 * static_cast<std::size_t>(k), 2), item)) {
            return false;
        }
        v[k] = item;
    }
    out = {static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]), static_cast<std::uint8_t>(v[2])};
    return true;
}

bool parse_category(const std::string& text, look::Category& out) {
    for (int i = 0; i < look::kCategories; ++i) {
        if (lower(text) == look::kCategoryColours[i].name) {
            out = static_cast<look::Category>(i);
            return true;
        }
    }
    return false;
}

bool read_pack(const std::string& folder, IconPack& out, std::vector<std::string>& problems) {
    out = IconPack{};
    out.folder = folder;
    std::error_code e;
    if (folder.empty() || !fs::is_directory(folder, e)) {
        return false;
    }
    const fs::path manifest = fs::path(folder) / "icons.json";
    if (fs::is_regular_file(manifest, e)) {
        out.manifest = true;
        std::vector<unsigned char> bytes;
        json::Value root;
        std::string why;
        if (!read_file(manifest.string(), bytes) || !json::parse(std::string(bytes.begin(), bytes.end()), root, why) ||
            root.kind != json::Value::Kind::Object) {
            problems.push_back(manifest.string() + ": not read (" + (why.empty() ? std::string("not an object") : why) + ")");
            return false;
        }
        auto text = [&root](const char* key) {
            const json::Value* v = root.find(key);
            return v != nullptr && v->is_string() ? v->string : std::string();
        };
        out.name = text("name");
        out.author = text("author");
        out.license = text("license");
        const json::Value* format = root.find("format");
        out.format = format != nullptr && format->is_number() ? static_cast<int>(format->number) : 0;
        if (out.format < 1 || out.format > kPackFormat) {
            problems.push_back(manifest.string() + ": format " + std::to_string(out.format) + " is not one this wheel reads (" +
                               std::to_string(kPackFormat) + ")");
            return false;
        }
        const json::Value* icons = root.find("icons");
        if (icons == nullptr || icons->kind != json::Value::Kind::Array) {
            problems.push_back(manifest.string() + ": no \"icons\" list");
            return false;
        }
        int n = 0;
        for (const json::Value& entry : icons->array) {
            ++n;
            const std::string where = manifest.string() + " icon " + std::to_string(n);
            auto field = [&entry](const char* key) {
                const json::Value* v = entry.find(key);
                return v != nullptr && v->is_string() ? v->string : std::string();
            };
            PackIcon icon;
            icon.pack = out.name.empty() ? folder : out.name;
            icon.game = field("game");
            const json::Value* item = entry.find("item");
            icon.special = field("special");
            const bool item_ok = item != nullptr
                                     ? (item->is_number() ? (icon.item = static_cast<int>(item->number), item->number >= 0 && item->number < 65536)
                                                          : item->is_string() && parse_item(item->string, icon.item))
                                     : !icon.special.empty();
            const std::string glyph = field("glyph");
            if (icon.game.empty() || !item_ok || !inside(glyph) || !glyph_file(glyph)) {
                problems.push_back(where + ": left out (it needs \"game\", \"item\" (or \"special\") and a \"glyph\" .svg or .png in the pack)");
                continue;
            }
            icon.file = (fs::path(folder) / glyph).string();
            const std::string category = field("category");
            if (!category.empty()) {
                icon.has_category = parse_category(category, icon.category);
                if (!icon.has_category) {
                    problems.push_back(where + ": category \"" + category + "\" is not one (its item's is used)");
                }
            }
            for (const auto& [key, has, colour] :
                 {std::tuple<const char*, bool*, look::Rgb*>{"wash", &icon.colours.has_wash, &icon.colours.wash},
                  {"glyph_color", &icon.colours.has_glyph, &icon.colours.glyph},
                  {"selected_glyph_color", &icon.colours.has_selected, &icon.colours.selected}}) {
                const std::string value = field(key);
                if (!value.empty()) {
                    *has = parse_colour(value, *colour);
                    if (!*has) {
                        problems.push_back(where + ": " + key + " \"" + value + "\" is not #RRGGBB (left as the category's)");
                    }
                }
            }
            out.icons.push_back(icon);
        }
        return true;
    }
    // Plain files: <game>/<item>.svg|png.
    std::vector<fs::path> games;
    for (const auto& entry : fs::directory_iterator(folder, e)) {
        if (entry.is_directory(e)) {
            games.push_back(entry.path());
        }
    }
    std::sort(games.begin(), games.end());
    for (const fs::path& game : games) {
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(game, e)) {
            if (entry.is_regular_file(e) && glyph_file(entry.path().filename().string())) {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            PackIcon icon;
            if (!parse_item(file.stem().string(), icon.item)) {
                problems.push_back(file.string() + ": left out (its name is not an item in hexadecimal)");
                continue;
            }
            icon.game = game.filename().string();
            icon.file = file.string();
            icon.pack = folder;
            out.icons.push_back(icon);
        }
    }
    return !out.icons.empty() || !problems.empty();
}

bool load_glyph(const std::string& path, Picture& out, std::string& why) {
    std::vector<unsigned char> bytes;
    if (!read_file(path, bytes) || bytes.empty()) {
        why = "cannot be read";
        return false;
    }
    if (lower(path).compare(path.size() - 4, 4, ".png") == 0) {
        int w = 0, h = 0, channels = 0;
        unsigned char* rgba = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
        if (rgba == nullptr) {
            why = "not a PNG";
            return false;
        }
        const bool ok = w <= 4096 && h <= 4096 && crop(rgba, w, h, out);
        stbi_image_free(rgba);
        why = ok ? "" : "no shape (all transparent) or too large";
        return ok;
    }
    bytes.push_back(0);
    NSVGimage* image = nsvgParse(reinterpret_cast<char*>(bytes.data()), "px", 96.0f);
    if (image == nullptr || image->shapes == nullptr || image->width <= 0.0f || image->height <= 0.0f) {
        if (image != nullptr) {
            nsvgDelete(image);
        }
        why = "not an SVG with a shape";
        return false;
    }
    // The view box drawn into kGlyphRaster square, kept in shape.
    const float scale = static_cast<float>(kGlyphRaster) / std::max(image->width, image->height);
    std::vector<unsigned char> rgba(static_cast<std::size_t>(kGlyphRaster) * kGlyphRaster * 4, 0);
    NSVGrasterizer* raster = nsvgCreateRasterizer();
    nsvgRasterize(raster, image, 0.0f, 0.0f, scale, rgba.data(), kGlyphRaster, kGlyphRaster, kGlyphRaster * 4);
    nsvgDeleteRasterizer(raster);
    nsvgDelete(image);
    if (!crop(rgba.data(), kGlyphRaster, kGlyphRaster, out)) {
        why = "no shape (nothing drawn)";
        return false;
    }
    return true;
}

void IconPacks::refresh(const std::vector<std::string>& folders, const std::string& game, std::vector<std::string>* log) {
    m_icons.clear();
    std::map<std::string, Cached> keep;
    for (const std::string& folder : folders) {
        IconPack pack;
        std::vector<std::string> problems;
        const bool read = read_pack(folder, pack, problems);
        if (log != nullptr) {
            log->insert(log->end(), problems.begin(), problems.end());
        }
        if (!read) {
            continue;
        }
        int used = 0, special = 0;
        for (const PackIcon& icon : pack.icons) {
            if (icon.game != game) {
                continue;
            }
            ++used;
            if (!icon.special.empty()) {
                ++special;  // not an item: never on the wheel
                continue;
            }
            const auto before = m_icons.find(icon.item);
            if (before != m_icons.end() && log != nullptr) {
                char item[16];
                std::snprintf(item, sizeof item, "0x%X", static_cast<unsigned>(icon.item));
                log->push_back(std::string("icon packs: item ") + item + ": " + icon.pack + "'s icon over " + before->second.pack + "'s");
            }
            m_icons[icon.item] = icon;
            // A picture already read is kept while its file is unchanged.
            std::error_code e;
            const auto cached = m_pictures.find(icon.file);
            if (cached != m_pictures.end() && cached->second.size == fs::file_size(icon.file, e) &&
                cached->second.time == write_time(icon.file)) {
                keep.insert(m_pictures.extract(cached));  // the same picture (it stays where it is)
            }
        }
        if (log != nullptr) {
            log->push_back("icon pack " + (pack.name.empty() ? folder : "\"" + pack.name + "\" (" + folder + ")") +
                           (pack.author.empty() ? "" : " by " + pack.author) +
                           (pack.license.empty() ? "" : ", licence: " + pack.license) + ": " + std::to_string(used) +
                           " icon(s) for " + game + (special > 0 ? " (" + std::to_string(special) + " of things that are not items)" : "") +
                           (pack.manifest ? "" : " (plain files)"));
        }
    }
    m_pictures.swap(keep);
}

const PackIcon* IconPacks::icon(int item) const {
    const auto it = m_icons.find(item);
    return it == m_icons.end() ? nullptr : &it->second;
}

const Picture* IconPacks::picture(const PackIcon& icon, std::string& why) {
    auto it = m_pictures.find(icon.file);
    if (it == m_pictures.end()) {
        Cached c;
        std::error_code e;
        c.size = fs::file_size(icon.file, e);
        c.time = write_time(icon.file);
        c.ok = load_glyph(icon.file, c.picture, c.why);
        it = m_pictures.emplace(icon.file, std::move(c)).first;
    }
    why = it->second.why;
    return it->second.ok ? &it->second.picture : nullptr;
}

}  // namespace spiral
