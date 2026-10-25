#include "glt/core/variant.hpp"

#include "glt/core/strings.hpp"

#include <windows.h>
#include <shlobj.h>

namespace glt::variant {
namespace {

constexpr std::wstring_view kDirName = L"GameLoopTuner";
constexpr std::wstring_view kFileName = L"variant.txt";

// %APPDATA% (FOLDERID_RoamingAppData). Empty only on a badly broken profile,
// which every caller treats as "no persisted choice".
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

const char* label(Id id) {
    switch (id) {
        case Id::Beta: return "GameLoop Beta";
        case Id::Hub:  return "GameLoop Hub (TAC)";
    }
    return "?";
}

const char* key(Id id) {
    switch (id) {
        case Id::Beta: return "beta";
        case Id::Hub:  return "hub";
    }
    return "?";
}

Id compiled() {
#if defined(GLT_TARGET_HUB)
    return Id::Hub;
#elif defined(GLT_TARGET_BETA)
    return Id::Beta;
#else
#error "Build must define exactly one of GLT_TARGET_BETA / GLT_TARGET_HUB (see native/CMakeLists.txt)"
#endif
}

Id default_id() { return Id::Beta; }

std::optional<Id> parse(std::string_view text) {
    const std::string t = str::ascii_lower(str::trim(text));
    if (t == "beta") return Id::Beta;
    if (t == "hub") return Id::Hub;
    // Tolerate the user-facing labels too, in case the file was hand-edited
    // before anyone read the format.
    if (t == "gameloop beta") return Id::Beta;
    if (t == "gameloop hub (tac)") return Id::Hub;
    return std::nullopt;
}

std::wstring choice_file() {
    const std::wstring base = roaming_appdata();
    if (base.empty()) return {};
    return str::path_join(str::path_join(base, kDirName), kFileName);
}

std::optional<Id> load_choice_from(const std::wstring& path) {
    if (path.empty()) return std::nullopt;

    std::string text;
    if (!str::read_file(path, text)) return std::nullopt;
    return parse(text);
}

bool save_choice_to(const std::wstring& path, Id id) {
    if (path.empty()) return false;

    const std::wstring dir = str::path_parent(path);
    if (!dir.empty() && !str::create_dirs(dir)) {
        // create_dirs returns false when the directory already exists on some
        // shells; only treat it as fatal when the directory is genuinely absent.
        if (!str::dir_exists(dir)) return false;
    }

    // Trailing newline so the file is a well-formed "line" for Notepad and for
    // any tool that reads it with getline.
    const std::string body = std::string(key(id)) + "\n";
    return str::write_file(path, body);
}

std::optional<Id> load_choice() { return load_choice_from(choice_file()); }

bool save_choice(Id id) { return save_choice_to(choice_file(), id); }

bool clear_choice() {
    const std::wstring path = choice_file();
    if (path.empty()) return false;
    if (!str::file_exists(path)) return true;
    return str::remove_file(path);
}

} // namespace glt::variant