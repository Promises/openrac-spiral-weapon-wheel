// SPDX-License-Identifier: GPL-3.0-or-later
//
// The icons' sources (1.3.0): the WAD decompressor, a level's HUD set read from a synthetic data
// file, the JSON reader, icon packs (icons.json and plain files, ICONS.md's example among them, the
// order they count in, the folders read again only when asked, at a level's load), and the mod's
// own glyphs read through the same loader as any pack. Synthetic pictures and the user's drawings
// only: nothing of the game's.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "icons_test_data.hpp"
#include "rac1/hud_set.hpp"
#include "rac1/wad.hpp"
#include "spiral/icon_packs.hpp"
#include "spiral/json.hpp"

namespace fs = std::filesystem;
namespace hud = gc_weapon_wheel::hud;

namespace {

int g_checks = 0, g_failures = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(c)) {                                                               \
            ++g_failures;                                                         \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

void test_wad() {
    // Literal runs between dummy packets, and little matches (one overlapping its own output).
    const std::vector<std::uint8_t> plain = {'a', 'b', 'c', 'd', 'a', 'b', 'c', 'd', 'x', 'y', 'z', 'z', 'z', 'z'};
    std::vector<std::uint8_t> packets = {0x01, 'a', 'b', 'c', 'd'};  // a literal of 4
    packets.insert(packets.end(), {0x4C, 0x00});                     // 3 from 4 back: "abc"
    packets.insert(packets.end(), {0x11, 0x01, 0x00, 'd'});          // a dummy with 1 little literal
    packets.insert(packets.end(), {0x01, 'x', 'y', 'z', 'z'});       // a literal of 4
    packets.insert(packets.end(), {0x40, 0x00});                     // 3 from 1 back: "zzz"
    std::vector<std::uint8_t> out;
    std::vector<std::uint8_t> wad = test_data::wad(packets);
    CHECK(hud::is_wad(wad.data(), wad.size()));
    CHECK(hud::wad_decompress(wad.data(), wad.size(), out));
    std::vector<std::uint8_t> want = plain;
    want.push_back('z');
    CHECK(out == want);
    // A long literal (18 + n), and test_data::compress's packing of any bytes.
    std::vector<std::uint8_t> bytes(5000);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>((i * 7) ^ (i >> 3));
    }
    wad = test_data::compress(bytes);
    CHECK(hud::wad_decompress(wad.data(), wad.size(), out) && out == bytes);
    // Damaged: a match before the start, two literals in a row, cut short, not a WAD.
    for (const std::vector<std::uint8_t>& bad : {std::vector<std::uint8_t>{0x40, 0x05}, std::vector<std::uint8_t>{0x01, 'a', 'b', 'c', 'd', 0x01, 'e', 'f', 'g', 'h'},
                                                std::vector<std::uint8_t>{0x05, 'a'}}) {
        const std::vector<std::uint8_t> w = test_data::wad(bad);
        CHECK(!hud::wad_decompress(w.data(), w.size(), out));
    }
    const std::uint8_t text[20] = "not a wad stream..";
    CHECK(!hud::is_wad(text, sizeof text) && !hud::wad_decompress(text, sizeof text, out));
}

void test_hud_set() {
    const test_data::LevelData level = test_data::level_data();
    std::vector<std::uint8_t> file = level.bytes;
    hud::Reader read = [&file](std::uint64_t offset, std::size_t size, std::vector<std::uint8_t>& out) {
        if (offset + size > file.size()) {
            return false;
        }
        out.assign(file.begin() + static_cast<std::ptrdiff_t>(offset), file.begin() + static_cast<std::ptrdiff_t>(offset + size));
        return true;
    };
    hud::HudSet set;
    std::string why;
    CHECK(set.load(read, why) && set.loaded());
    if (!set.loaded()) {
        std::fprintf(stderr, "the HUD set: %s\n", why.c_str());
    }
    // GetIconFrame: an icon's first frame + k; 0 for a missing icon or k past its frames.
    CHECK(set.has_icon(test_data::kIconRing) && !set.has_icon(0x1234));
    CHECK(set.icon_frame(test_data::kIconRing, 0) == 1 && set.icon_frame(test_data::kIconRing, 4) == 5);
    CHECK(set.icon_frame(test_data::kIconRing, 5) == 0 && set.icon_frame(0x1234, 0) == 0);
    CHECK(set.icon_frame(test_data::kIconThick, 0) == 6);
    // A frame's picture: the texture from bank 0 (WAD), the palette from bank 1 (stored), in index
    // order (CSM1 undone).
    hud::Indexed pic;
    CHECK(set.frame_picture(1, pic) && pic.width == 32 && pic.height == 32 && pic.indices.size() == 1024u);
    CHECK(pic.indices == level.ring_indices);
    CHECK(std::memcmp(pic.palette.data(), level.palette.data(), 1024) == 0);
    CHECK(set.frame_picture(6, pic) && pic.indices == level.thick_indices);
    CHECK(!set.frame_picture(0, pic));   // frame 0: no texture
    CHECK(!set.frame_picture(99, pic));  // past the frames
    // A data file without its banks: the header reads, the pictures do not.
    file = test_data::level_data(true).bytes;
    CHECK(set.load(read, why) && !set.frame_picture(1, pic));
    // Not a level's data: refused, with a reason.
    file.assign(16, 0);
    CHECK(!set.load(read, why) && !why.empty() && !set.loaded());
    // The path under a game's data folder.
    CHECK(hud::level_data_path("/data", "rac1", 3) == "/data/iso_data/rac1/lumps/levels/03/data.bin");
}

void test_json() {
    namespace json = spiral::json;
    json::Value v;
    std::string why;
    CHECK(json::parse("{\"a\": [1, -2.5e1, true, false, null], \"b\": {\"c\": \"x\\\"y\\u00e9\\n\"}}", v, why));
    const json::Value* a = v.find("a");
    CHECK(a != nullptr && a->array.size() == 5u && a->array[1].number == -25.0 && a->array[2].boolean);
    CHECK(a->array[4].kind == json::Value::Kind::Null);
    const json::Value* b = v.find("b");
    CHECK(b != nullptr && b->find("c") != nullptr && b->find("c")->string == "x\"y\xC3\xA9\n");
    CHECK(v.find("missing") == nullptr);
    for (const char* bad : {"{", "{\"a\" 1}", "[1,]", "{\"a\": 1} x", "\"no end", "{'a': 1}", "[1 2]"}) {
        CHECK(!json::parse(bad, v, why) && why.find("line") != std::string::npos);
    }
    CHECK(json::parse("\xEF\xBB\xBF{}", v, why) && v.kind == json::Value::Kind::Object);  // a BOM
}

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
void write(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A square glyph of `size` in a 256 view box, as the mod's own are drawn.
std::string svg(int size) {
    const int at = (256 - size) / 2;
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 256\" width=\"256\" height=\"256\"><path "
           "fill=\"currentColor\" d=\"M" + std::to_string(at) + " " + std::to_string(at) + "h" + std::to_string(size) + "v" +
           std::to_string(size) + "h-" + std::to_string(size) + "z\"/></svg>";
}

void test_names() {
    int item = 0;
    CHECK(spiral::parse_item("0x0A", item) && item == 0x0A);
    CHECK(spiral::parse_item("0a", item) && item == 0x0A);
    CHECK(spiral::parse_item("A", item) && item == 0x0A);
    CHECK(spiral::parse_item("24", item) && item == 0x24);
    CHECK(spiral::parse_item("0x12345", item) && item == 0x12345);  // any item id, past the game's tables
    CHECK(!spiral::parse_item("bomb", item) && !spiral::parse_item("", item) && !spiral::parse_item("123456789", item));
    spiral::look::Rgb c{};
    CHECK(spiral::parse_colour("#C49257", c) && c.r == 0xC4 && c.g == 0x92 && c.b == 0x57);
    CHECK(!spiral::parse_colour("C49257", c) && !spiral::parse_colour("#C4925", c) && !spiral::parse_colour("#GG0000", c));
    spiral::look::Category k{};
    for (int i = 0; i < spiral::look::kCategories; ++i) {
        CHECK(spiral::parse_category(spiral::look::kCategoryColours[i].name, k) && static_cast<int>(k) == i);
    }
    CHECK(spiral::parse_category("Gold", k) && k == spiral::look::Category::Gold);
    CHECK(!spiral::parse_category("shoes", k));
}

fs::path temp_root(const char* what) {
    const fs::path root = fs::temp_directory_path() /
                          (std::string(what) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

// ICONS.md's example pack, as the guide writes it.
const char* kExampleManifest = R"({
  "format": 1,
  "name": "Laser Pistol",
  "author": "A. Modder",
  "license": "CC BY 4.0",
  "icons": [
    {"game": "rac1", "item": "0x24", "glyph": "laser-pistol.svg", "category": "weapon"},
    {"game": "rac1", "item": 10, "glyph": "glyphs/bomb.png", "category": "weapon",
     "wash": "#803030", "glyph_color": "#FFD0D0", "selected_glyph_color": "#FFFFFF",
     "wrap": "Laser-|Pistol"}
  ]
})";

void test_packs() {
    const fs::path root = temp_root("spiral-packs-test");
    // A pack with icons.json: its icons, categories and colours; the bad entries left out, said why.
    const fs::path example = root / "plugins" / "laser-mod" / "spiral-wheel-icons";
    write(example / "icons.json", std::string(kExampleManifest));
    write(example / "laser-pistol.svg", svg(100));
    write(example / "glyphs" / "bomb.png", test_data::png(10, 6, 255, 255, 255, 255));
    spiral::IconPack pack;
    std::vector<std::string> problems;
    CHECK(spiral::read_pack(example.string(), pack, problems) && problems.empty());
    CHECK(pack.manifest && pack.format == 1 && pack.name == "Laser Pistol" && pack.author == "A. Modder" &&
          pack.license == "CC BY 4.0");
    CHECK(pack.icons.size() == 2u);
    if (pack.icons.size() == 2u) {
        CHECK(pack.icons[0].item == 0x24 && pack.icons[0].has_category && pack.icons[0].category == spiral::look::Category::Weapon);
        CHECK(!pack.icons[0].colours.has_wash);
        CHECK(pack.icons[1].wrap == "Laser-|Pistol" && pack.icons[0].wrap.empty());
        CHECK(pack.icons[1].item == 10 && pack.icons[1].colours.has_wash && pack.icons[1].colours.wash.r == 0x80 &&
              pack.icons[1].colours.has_glyph && pack.icons[1].colours.has_selected && pack.icons[1].colours.selected.g == 0xFF);
    }
    const fs::path bad = root / "bad";
    write(bad / "icons.json", std::string(R"({"format": 1, "icons": [
        {"game": "rac1", "item": "0x09", "glyph": "../outside.svg"},
        {"game": "rac1", "item": "zz", "glyph": "a.svg"},
        {"game": "rac1", "item": "0x0A", "glyph": "a.gif"},
        {"game": "rac1", "item": "0x0B", "glyph": "b.svg", "category": "shoes", "wash": "red", "wrap": "nobreak"}]})"));
    CHECK(spiral::read_pack(bad.string(), pack, problems) && pack.icons.size() == 1u && problems.size() == 6u);
    CHECK(pack.icons.size() == 1u && pack.icons[0].wrap.empty());  // a wrap without "|" left out
    problems.clear();
    write(bad / "icons.json", std::string(R"({"format": 2, "icons": []})"));
    CHECK(!spiral::read_pack(bad.string(), pack, problems) && problems.size() == 1u &&
          problems[0].find("format 2") != std::string::npos);
    problems.clear();
    write(bad / "icons.json", std::string("{not json"));
    CHECK(!spiral::read_pack(bad.string(), pack, problems) && problems.size() == 1u);
    problems.clear();
    CHECK(!spiral::read_pack((root / "none").string(), pack, problems) && problems.empty());
    // Plain files: <game>/<item>.svg|png, no categories or colours.
    const fs::path plain = root / "plugins" / "spiral-weapon-wheel" / "icons";
    write(plain / "rac1" / "0a.svg", svg(60));
    write(plain / "rac1" / "0x0B.png", test_data::png(8, 8, 255, 255, 255, 255));
    write(plain / "rac1" / "readme.txt", std::string("x"));
    write(plain / "rac2" / "0a.svg", svg(30));
    CHECK(spiral::read_pack(plain.string(), pack, problems) && !pack.manifest && pack.icons.size() == 3u);
    // The glyphs' pictures: an SVG drawn at 256 and cropped to its shape (a pixel of room), white;
    // a PNG cropped to its alpha.
    spiral::Picture picture;
    std::string why;
    CHECK(spiral::load_glyph((example / "laser-pistol.svg").string(), picture, why));
    CHECK(picture.width == 102 && picture.height == 102 && picture.rgba[0] == 255);
    CHECK(picture.rgba[3] == 0 && picture.rgba[(51 * 102 + 51) * 4 + 3] == 255);
    CHECK(spiral::load_glyph((example / "glyphs" / "bomb.png").string(), picture, why) && picture.width == 10 &&
          picture.height == 6);
    write(root / "empty.svg", std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 256\"></svg>"));
    CHECK(!spiral::load_glyph((root / "empty.svg").string(), picture, why) && !why.empty());
    // The order they count in: the mod's own, then other mods', then the player's (here the
    // player's plain 0a over the laser mod's 10); each icon a later pack takes over is logged.
    const fs::path own = root / "package" / "assets" / "glyphs" / "rac1";
    write(own / "icons.json", std::string(R"({"format": 1, "name": "Built-in", "icons": [
        {"game": "rac1", "item": "0x0A", "glyph": "a.svg", "category": "weapon"},
        {"game": "rac1", "item": "0x0C", "glyph": "c.svg", "category": "gadget"}]})"));
    write(own / "a.svg", svg(200));
    write(own / "c.svg", svg(180));
    spiral::IconPacks packs;
    std::vector<std::string> log;
    packs.refresh({own.string(), example.string(), plain.string()}, "rac1", &log);
    CHECK(packs.icons() == 4u);  // 0x0A, 0x0B, 0x0C, 0x24
    const spiral::PackIcon* a = packs.icon(0x0A);
    CHECK(a != nullptr && a->file.find("icons") != std::string::npos && !a->has_category);
    CHECK(packs.icon(0x0C) != nullptr && packs.icon(0x0C)->category == spiral::look::Category::Gadget);
    CHECK(packs.icon(0x24) != nullptr && packs.icon(0x24)->pack == "Laser Pistol");
    int overridden = 0;
    for (const std::string& line : log) {
        overridden += line.find("item 0xA:") != std::string::npos;
    }
    CHECK(overridden == 2);  // the laser mod's over the built-in, the player's over the laser mod's
    CHECK(packs.picture(*packs.icon(0x0A), why) != nullptr && packs.picture(*packs.icon(0x0A), why)->width == 62);
    // A file changed: the picture stays until the folders are read again (a level's load).
    const spiral::Picture* kept = packs.picture(*packs.icon(0x0C), why);
    write(plain / "rac1" / "0a.svg", svg(120));
    CHECK(packs.picture(*packs.icon(0x0A), why)->width == 62);
    packs.refresh({own.string(), example.string(), plain.string()}, "rac1");
    CHECK(packs.picture(*packs.icon(0x0A), why)->width == 122);
    CHECK(packs.picture(*packs.icon(0x0C), why) == kept);  // unchanged: not drawn again
    // A pack gone: its icons go with it.
    fs::remove_all(plain);
    packs.refresh({own.string(), example.string(), plain.string()}, "rac1");
    CHECK(packs.icon(0x0B) == nullptr && packs.icon(0x0A) != nullptr && packs.icon(0x0A)->pack == "Laser Pistol");
    fs::remove_all(root);
}

// The mod's own glyphs, read through the same loader as any pack: every icon of icons.json drawn,
// at the categories of the game's menus (the Quick Select page's weapons and hand gadgets, the
// packs, head and foot grids, the other items).
void test_builtin_pack() {
    const std::string folder = std::string(SPIRAL_SOURCE_DIR) + "/assets/glyphs/rac1";
    spiral::IconPacks packs;
    std::vector<std::string> log;
    packs.refresh({folder}, "rac1", &log);
    CHECK(packs.icons() == 33u);
    CHECK(!log.empty() && log.back().find("\"Spiral Weapon Wheel glyphs\"") != std::string::npos &&
          log.back().find("by Henning Berge") != std::string::npos);
    using C = spiral::look::Category;
    const std::vector<std::pair<C, std::vector<int>>> groups = {
        {C::Weapon, {0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19}},
        {C::Gadget, {0x0C, 0x16, 0x1A, 0x1B, 0x1F, 0x20}},
        {C::Pack, {0x02, 0x03, 0x04}},
        {C::Head, {0x05, 0x06, 0x07}},
        {C::Feet, {0x1C, 0x1D}},
        {C::Item, {0x1E, 0x21, 0x22, 0x23}}};
    std::size_t listed = 0;
    for (const auto& [category, items] : groups) {
        for (const int item : items) {
            ++listed;
            const spiral::PackIcon* icon = packs.icon(item);
            CHECK(icon != nullptr);
            if (icon == nullptr) {
                continue;
            }
            CHECK(icon->has_category && icon->category == category);
            std::string why;
            const spiral::Picture* p = packs.picture(*icon, why);
            CHECK(p != nullptr && p->width > 64 && p->height > 64 && p->width <= 256 && p->height <= 256);
        }
    }
    CHECK(listed == 33u);
    CHECK(packs.icon(0x08) == nullptr);  // the OmniWrench: no glyph (it is never on the quick select)
    // The wrap hints: the single long words the middle disc cannot hold on one line.
    CHECK(packs.icon(0x16) != nullptr && packs.icon(0x16)->wrap == "Hydro-|displacer");
    CHECK(packs.icon(0x15) != nullptr && packs.icon(0x15)->wrap == "Morph-o-|Ray");
    CHECK(packs.icon(0x21) != nullptr && packs.icon(0x21)->wrap == "Map-o-|Matic");
    CHECK(packs.icon(0x03) != nullptr && packs.icon(0x03)->wrap == "Thruster-|Pack");
    CHECK(packs.icon(0x0A) != nullptr && packs.icon(0x0A)->wrap.empty());
    // All 38 drawings are in the pack: the 5 things RAC1 keeps as flags of their own, not items
    // (the Zoomerator, Raritanium, the Codebot, Premium and Ultra Nanotech), by name, each drawn.
    spiral::IconPack pack;
    std::vector<std::string> problems;
    CHECK(spiral::read_pack(folder, pack, problems) && problems.empty() && pack.icons.size() == 38u);
    int specials = 0;
    for (const spiral::PackIcon& icon : pack.icons) {
        spiral::Picture picture;
        std::string why;
        CHECK(spiral::load_glyph(icon.file, picture, why));
        specials += !icon.special.empty();
        CHECK(icon.special.empty() || (icon.has_category && icon.category == C::Item));
    }
    CHECK(specials == 5);
    CHECK(log.back().find("(5 of things that are not items)") != std::string::npos);
}

}  // namespace

int main() {
    test_wad();
    test_hud_set();
    test_json();
    test_names();
    test_packs();
    test_builtin_pack();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
