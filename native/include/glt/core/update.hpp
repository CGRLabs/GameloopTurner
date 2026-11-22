// ─────────────────────────────────────────────────────────────────────────────
// glt/core/update.hpp — self-update from GitHub Releases.
//
// DESIGN / SECURITY CONTRACT
// --------------------------
// The tuner ships as a single exe and installs per-machine. The updater:
//
//   1. GETs https://api.github.com/repos/CGRLabs/GameloopTurner/releases/latest
//      over HTTPS (WinHTTP, system proxy honoured) with a proper User-Agent —
//      GitHub's API rejects requests without one. (The repository moved from
//      realLLUKS to CGRLabs; the old name no longer resolves on the API.)
//   2. Picks the asset for THIS build: the setup exe when the release attaches
//      one directly, else the portable exe for the compiled variant
//      (GameLoopTuner-Beta.exe / GameLoopTuner-Hub.exe — the current release
//      workflow publishes per-variant exes and zips, not a bare setup).
//   3. Downloads it to a temp location, hashing SHA-256 while the bytes stream
//      in (BCrypt), and REFUSES to install unless the hash matches the digest
//      the release API reports. A release without a digest is treated as
//      unusable, not as "trust it".
//   4. Installs:
//        · setup asset  → launch via ShellExecute "runas" (it self-elevates
//          and replaces the app), then the tuner exits;
//        · portable exe → verified self-replace: rename the running binary to
//          .old, move the new one into its place, relaunch, exit. The cleanup
//          of the .old file happens on the next launch
//          (cleanup_previous_update()).
//
// Nothing here runs, replaces or deletes any file by itself: every install is
// user-confirmed (App::install_update asks). The pure parts — version compare
// with prerelease tags, asset selection, hashing — are unit-testable without
// network; the live path is exercised by `GameLoopTuner.exe --update-check`.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace glt::update {

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& what) : std::runtime_error(what) {}
};

// ── Versions ─────────────────────────────────────────────────────────────────

// "v2.1.0" / "2.1.0" / "v2" → {2,1,0} / {2,1,0} / {2,0,0}. A prerelease label
// after three numeric components ("1.2.0.beta") is tolerated and ignored for
// comparison — the published tags are not always pure semver. Anything else
// that is not a dot-separated number run → nullopt: a garbage tag must never
// win a compare, and "2.0.0.1" is refused outright.
struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
};
[[nodiscard]] std::optional<SemVer> parse_version(std::string_view tag);

// True when `tag` is a strictly newer release than `current` (both accept the
// optional leading 'v'). Unparseable input returns false — never guess.
[[nodiscard]] bool is_newer(std::string_view tag, std::string_view current);

// ── Release discovery ────────────────────────────────────────────────────────

enum class Kind {
    None,       // valid release, but no asset usable by this build
    Installer,  // GameLoopTunerSetup.exe attached directly
    Portable,   // the compiled variant's exe (GameLoopTuner-Beta/-Hub.exe)
};

struct Release {
    std::string tag;         // "v2.1.0"
    std::string page_url;    // release html_url
    Kind kind = Kind::None;
    std::string asset_name;  // "" when kind == None
    std::string asset_url;   // browser_download_url
    std::uint64_t asset_size = 0;
    std::string sha256;      // lowercase hex, from the release digest field
    std::string notes;       // release body markdown (may be empty)
};

// Selects the usable asset from a /releases/latest JSON body. `variant_exe`
// is the portable exe name matching the compiled variant ("GameLoopTuner-
// Beta.exe"). Preference order: setup exe, variant exe. A candidate without
// a digest is skipped, not trusted. Kind::None (with tag/page kept) when the
// release is valid but nothing usable ships with it — the UI can still send
// the user to the release page. nullopt only for an unusable body.
// Exposed so the selection rules are testable without network.
[[nodiscard]] std::optional<Release>
parse_latest_json(std::string_view body, std::string_view variant_exe);

// The live call: fetches and parses the latest release. Throws update::Error
// with a human-readable reason (offline, rate-limited, malformed body).
[[nodiscard]] Release fetch_latest();

// ── Download + verify ────────────────────────────────────────────────────────

// Streams r.asset_url into `dest`, hashing as it goes. progress (bytes done,
// bytes total; total may be 0 when the server sends no length). Throws
// update::Error on any failure AND on digest mismatch when r.sha256 is set;
// a failed or mismatched download is deleted, never left behind.
// Returns the lowercase-hex SHA-256 of the file on disk.
std::string download_verified(const Release& r, const std::wstring& dest,
                              const std::function<void(std::uint64_t, std::uint64_t)>& progress);

// SHA-256 of a file, lowercase hex. "" on failure. (Public because the UI
// re-verifies the downloaded file before offering Install.)
[[nodiscard]] std::string sha256_file(const std::wstring& path);

// ── Install ──────────────────────────────────────────────────────────────────

// Launches the setup exe via the "runas" verb (it self-elevates and shows its
// own UI). Returns false and fills `error` when the user declines UAC or the
// launch fails. Does NOT exit the app — the caller decides when to quit.
bool launch_installer(const std::wstring& setup_path, std::string& error);

// Portable self-replace. `new_exe_path` must be a VERIFIED download; it is
// moved over the running binary (whose own name and directory are kept, so a
// renamed copy stays renamed), the old image is left as <name>.old for the
// next launch to delete, and the new binary is relaunched. Returns false and
// fills `error` without touching anything when any step fails — the running
// app keeps working either way.
bool self_replace(const std::wstring& new_exe_path, std::string& error);

// Deletes <running exe>.old from a previous self-replace. Best-effort, silent:
// called once at startup while the previous binary is definitely not running.
void cleanup_previous_update();

// The directory holding the running exe — where a portable self-replace has to
// stage its download so the final move is a same-volume rename (a cross-volume
// MoveFileEx degenerates to copy+delete, which cannot overwrite a running
// image). "" on failure.
[[nodiscard]] std::wstring running_exe_dir();

// The well-known asset names. The setup is preferred when attached directly;
// otherwise the updater takes the portable exe matching the compiled variant.
inline constexpr const char* kSetupAsset = "GameLoopTunerSetup.exe";
inline constexpr const char* kBetaExe = "GameLoopTuner-Beta.exe";
inline constexpr const char* kHubExe = "GameLoopTuner-Hub.exe";

// The repository this build updates itself from. CGRLabs is the current home;
// the old realLLUKS name no longer resolves on the API (HTTP 404). Edit this
// together with the About page links; it is deliberately not a runtime setting.
inline constexpr const char* kRepoApiLatest =
    "/repos/CGRLabs/GameloopTurner/releases/latest";
inline constexpr const wchar_t* kRepoApiHost = L"api.github.com";

} // namespace glt::update
