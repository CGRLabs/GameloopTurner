// glt/core/i18n.cpp — see glt/core/i18n.hpp for the design notes.
#include "glt/core/i18n.hpp"

#include "glt/core/log.hpp"
#include "glt/core/strings.hpp"

#include <windows.h>
#include <shlobj.h>

#include <unordered_map>

namespace glt::i18n {
namespace {

// ── The catalogue ────────────────────────────────────────────────────────────
//
// The language LIST is fixed here so the picker can offer a language even
// while its table is still being written; a language without a table simply
// falls back to English through tr()'s miss path. "en" must stay first.
const std::vector<Lang>& catalogue() {
    static const std::vector<Lang> kLangs = {
        {"en", "English"},
        {"ar", "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9"},            // العربية
        {"es", "Espa\xC3\xB1ol"},                                                        // Español
        {"pt", "Portugu\xC3\xAAs"},                                                      // Português
        {"id", "Bahasa Indonesia"},
        {"ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"},              // Русский
        {"tr", "T\xC3\xBCrk\xC3\xA7\xE7"},                                               // Türkçe
        {"vi", "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t"},                                      // Tiếng Việt
    };
    return kLangs;
}

// ── Parsed tables ────────────────────────────────────────────────────────────
//
// Parsed lazily on first use and kept for process lifetime, so tr() is a hash
// lookup after the first frame. The English source is not a table at all — it
// is the key space itself.
using Table = std::unordered_map<std::string, std::string>;

std::unordered_map<std::string, Table>& tables() {
    static std::unordered_map<std::string, Table> inst;
    return inst;
}

std::string g_current = "en";

// Builds the lookup table for one language by zipping the shared key list
// with that language's positional value array. Empty values are skipped —
// they mean "not translated", and tr()'s miss path supplies the English key
// itself. Nothing here can fail: the arrays are compile-time checked against
// the key count, so there is no parse and no error path.
bool load_table(const std::string& code) {
    auto& t = tables();
    if (t.count(code)) return true;

    const data::Entry* entry = nullptr;
    for (const auto& e : data::entries()) {
        if (code == e.code) {
            entry = &e;
            break;
        }
    }
    if (entry == nullptr) return false; // offered but not shipped yet

    Table table;
    const auto ks = data::keys();
    const std::size_t n = data::key_count();
    for (std::size_t i = 0; i < n; ++i) {
        const char* v = entry->values[i];
        if (v == nullptr || v[0] == '\0') continue;
        table.emplace(ks[i], v);
    }
    t.emplace(code, std::move(table));
    return true;
}

// ── Persistence (mirrors variant.cpp's arrangement) ──────────────────────────

constexpr std::wstring_view kDirName = L"GameLoopTuner";
constexpr std::wstring_view kFileName = L"language.txt";

std::wstring roaming_appdata() {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        return {};
    }
    std::wstring out(raw);
    ::CoTaskMemFree(raw);
    return out;
}

} // namespace

// ── Languages ────────────────────────────────────────────────────────────────

const std::vector<Lang>& languages() { return catalogue(); }

std::string_view current() { return g_current; }

bool set_language(std::string_view code) {
    const std::string c = str::ascii_lower(str::trim(code));
    if (c == "en") {
        g_current = "en";
        return true;
    }
    bool known = false;
    for (const auto& l : catalogue()) {
        if (c == l.code) {
            known = true;
            break;
        }
    }
    if (!known) return false;
    if (!load_table(c)) return false;
    g_current = c;
    return true;
}

std::string detect_system_language() {
    const LANGID lang = ::GetUserDefaultUILanguage();
    if (lang == 0) return "en";
    switch (PRIMARYLANGID(lang)) {
        case LANG_ARABIC:     return "ar";
        case LANG_SPANISH:    return "es";
        case LANG_PORTUGUESE: return "pt";
        case LANG_INDONESIAN: return "id";
        case LANG_RUSSIAN:    return "ru";
        case LANG_TURKISH:    return "tr";
        case LANG_VIETNAMESE: return "vi";
        default:              return "en";
    }
}

std::string init() {
    const auto saved = load_choice();
    std::string code = "en";

    if (saved) {
        const std::string s = str::ascii_lower(str::trim(*saved));
        if (s != "auto" && s != "en") code = s;
    }
    if (code == "en" && (!saved || str::ascii_lower(str::trim(*saved)) != "en")) {
        // No explicit English choice either: follow Windows.
        code = detect_system_language();
    }

    if (!set_language(code)) {
        set_language("en");
        code = "en";
    }
    return code;
}

// ── Translation ──────────────────────────────────────────────────────────────

const char* tr(const char* en) {
    if (en == nullptr) return nullptr;
    if (g_current == "en") return en;

    const auto& t = tables();
    const auto it = t.find(g_current);
    if (it == t.end()) return en;
    const auto hit = it->second.find(en);
    return hit != it->second.end() ? hit->second.c_str() : en;
}

// ── Persistence ──────────────────────────────────────────────────────────────

std::wstring choice_file() {
    const std::wstring base = roaming_appdata();
    if (base.empty()) return {};
    return str::path_join(str::path_join(base, kDirName), kFileName);
}

std::optional<std::string> load_choice() {
    const std::wstring file = choice_file();
    if (file.empty() || !str::file_exists(file)) return std::nullopt;

    std::string text;
    if (!str::read_file(file, text)) return std::nullopt;

    std::string_view v = str::trim(text);
    // Reject an empty/hand-emptied file the same way as a missing one: the
    // user gets auto-detection rather than a broken startup.
    if (v.empty()) return std::nullopt;
    return std::string(v);
}

bool save_choice(std::string_view code) {
    const std::wstring file = choice_file();
    if (file.empty()) return false;
    if (!str::create_dirs(str::path_parent(file))) return false;
    return str::write_file(file, code);
}

} // namespace glt::i18n
