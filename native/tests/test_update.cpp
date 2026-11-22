// ─────────────────────────────────────────────────────────────────────────────
// update tests — version math, release selection, and hashing.
//
// The network layer is deliberately NOT tested here: ctest must not depend on
// api.github.com. What the tests pin down is everything that decides whether
// a downloaded installer is allowed to run:
//   * version compare (a lexicographic bug would compare "10.0.0" < "9.0.0"),
//   * asset selection (no digest → REFUSED — that is the security contract),
//   * SHA-256 known vectors (the hash is what gates install).
// The live path is exercised by `GameLoopTuner.exe --update-check`.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/strings.hpp"
#include "glt/core/update.hpp"

#include <windows.h>

#include <cstdio>
#include <string>

using namespace glt::update;

// ── Version parsing ──────────────────────────────────────────────────────────

GLT_TEST(UpdateVersion, parses_tags_with_and_without_v) {
    const auto a = parse_version("v2.0.1");
    CHECK(a.has_value());
    if (a) {
        CHECK_EQ(a->major, 2);
        CHECK_EQ(a->minor, 0);
        CHECK_EQ(a->patch, 1);
    }

    const auto b = parse_version("2.10.3");
    CHECK(b.has_value());
    if (b) {
        CHECK_EQ(b->major, 2);
        CHECK_EQ(b->minor, 10);
        CHECK_EQ(b->patch, 3);
    }

    // Short forms fill from the left: "v2" is major 2, minor/patch 0.
    const auto c = parse_version("v2");
    CHECK(c.has_value());
    if (c) {
        CHECK_EQ(c->major, 2);
        CHECK_EQ(c->minor, 0);
        CHECK_EQ(c->patch, 0);
    }
}

GLT_TEST(UpdateVersion, refuses_garbage_instead_of_guessing) {
    CHECK(!parse_version("").has_value());
    CHECK(!parse_version("v").has_value());
    CHECK(!parse_version("abc").has_value());
    CHECK(!parse_version("v2.x").has_value());
    CHECK(!parse_version("v2.").has_value());
    CHECK(!parse_version("2..0").has_value());
    CHECK(!parse_version("2.0.0.1").has_value());   // four segments: refuse
    CHECK(!parse_version("2.0.99999999999").has_value()); // 11-digit component
    CHECK(!parse_version("vv2").has_value());
}

GLT_TEST(UpdateVersion, tolerates_prerelease_labels) {
    // The published tags are not always pure semver: the repo's current latest
    // is "1.2.0.beta". The numeric prefix is what compares.
    const auto a = parse_version("1.2.0.beta");
    CHECK(a.has_value());
    if (a) {
        CHECK_EQ(a->major, 1);
        CHECK_EQ(a->minor, 2);
        CHECK_EQ(a->patch, 0);
    }
    CHECK(!is_newer("1.2.0.beta", "v2.0.0"));  // old prerelease vs current
    CHECK(is_newer("v2.1.0.beta", "2.0.0"));   // a beta of a newer line
}

GLT_TEST(UpdateIsNewer, numeric_compare_not_lexicographic) {
    CHECK(is_newer("v2.0.1", "2.0.0"));
    CHECK(is_newer("2.1.0", "v2.0.9"));
    CHECK(is_newer("v10.0.0", "v9.99.99")); // the bug this test exists for

    CHECK(!is_newer("2.0.0", "2.0.0"));
    CHECK(!is_newer("v1.9.9", "2.0.0"));
    CHECK(!is_newer("v2.0.0", "v2.0.1"));
    CHECK(!is_newer("v2.0", "v2.0.1")); // missing patch counts as 0
}

GLT_TEST(UpdateIsNewer, unparseable_input_never_wins) {
    // A garbage tag must not trigger an update, and a garbage "current" must
    // not make everything look older than it is.
    CHECK(!is_newer("garbage", "2.0.0"));
    CHECK(!is_newer("2.0.1", "garbage"));
    CHECK(!is_newer("", ""));
}

// ── Release selection ────────────────────────────────────────────────────────

namespace {

constexpr const char* kBody = R"json({
  "tag_name": "v2.1.0",
  "html_url": "https://github.com/realLLUKS/GameloopTurner/releases/tag/v2.1.0",
  "body": "Fixed things.",
  "assets": [
    {"name": "README.txt",
     "browser_download_url": "https://github.com/x/readme.txt",
     "size": 10,
     "digest": "sha256:00"},
    {"name": "GameLoopTunerSetup.exe",
     "browser_download_url": "https://github.com/x/GameLoopTunerSetup.exe",
     "size": 1835008,
     "digest": "sha256:ABCDEF0123456789"}
  ]
})json";

} // namespace

GLT_TEST(UpdateJson, selects_the_setup_asset_and_normalises_the_digest) {
    const auto r = parse_latest_json(kBody, kHubExe);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->kind == Kind::Installer); // the setup wins wherever it appears
    CHECK_EQ(r->tag, std::string("v2.1.0"));
    CHECK_EQ(r->asset_name, std::string(kSetupAsset));
    CHECK_EQ(r->asset_url, std::string("https://github.com/x/GameLoopTunerSetup.exe"));
    CHECK_EQ(r->asset_size, std::uint64_t{1835008});
    // Digest is lowercased: the computed hash is lowercase hex.
    CHECK_EQ(r->sha256, std::string("abcdef0123456789"));
    CHECK_EQ(r->page_url,
             std::string("https://github.com/realLLUKS/GameloopTurner/releases/tag/v2.1.0"));
}

GLT_TEST(UpdateJson, falls_back_to_the_variant_portable_exe) {
    // The CURRENT release workflow attaches per-variant portable exes and
    // zips; the setup only exists inside the zips. A Beta build must pick its
    // own exe and mark it portable.
    std::string body = R"json({
      "tag_name": "v2.1.0",
      "html_url": "https://x/tag/v2.1.0",
      "assets": [
        {"name": "GameLoopTuner-Beta.zip", "digest": "sha256:01"},
        {"name": "GameLoopTuner-Beta.exe",
         "browser_download_url": "https://x/beta.exe",
         "size": 1939456, "digest": "sha256:BF6C"},
        {"name": "GameLoopTuner-Hub.exe",
         "browser_download_url": "https://x/hub.exe",
         "size": 1939456, "digest": "sha256:FF"},
        {"name": "SHA256SUMS.txt", "digest": "sha256:02"}
      ]
    })json";

    const auto beta = parse_latest_json(body, kBetaExe);
    CHECK(beta.has_value());
    if (beta) {
        CHECK(beta->kind == Kind::Portable);
        CHECK_EQ(beta->asset_name, std::string(kBetaExe));
        CHECK_EQ(beta->sha256, std::string("bf6c"));
        CHECK_NE(beta->asset_name, std::string(kHubExe)); // never another variant's exe
    }

    const auto hub = parse_latest_json(body, kHubExe);
    CHECK(hub.has_value());
    if (hub) {
        CHECK(hub->kind == Kind::Portable);
        CHECK_EQ(hub->asset_name, std::string(kHubExe));
    }
}

GLT_TEST(UpdateJson, valid_release_without_usable_assets_is_none_not_an_error) {
    // A newer tag that ships nothing this build can install must still surface
    // (the About card offers the release page) rather than read as a failure.
    std::string body = R"json({
      "tag_name": "v3.0.0",
      "html_url": "https://x/tag/v3.0.0",
      "assets": [
        {"name": "GameLoopTuner-Beta.zip", "digest": "sha256:01"},
        {"name": "SHA256SUMS.txt", "digest": "sha256:02"}
      ]
    })json";
    const auto r = parse_latest_json(body, kBetaExe);
    CHECK(r.has_value());
    if (r) {
        CHECK(r->kind == Kind::None);
        CHECK_EQ(r->tag, std::string("v3.0.0"));
    }
}

GLT_TEST(UpdateJson, refuses_a_release_without_a_digest) {
    // SECURITY: this is the load-bearing refusal. An asset without a digest is
    // treated as unusable, not as "trust it".
    std::string no_digest = R"json({
      "tag_name": "v2.1.0",
      "assets": [{"name": "GameLoopTunerSetup.exe",
                  "browser_download_url": "https://x/setup.exe", "size": 5}]
    })json";
    // A digestless setup is skipped, not trusted — and with nothing else
    // usable the release is Kind::None, still valid JSON-wise.
    const auto r = parse_latest_json(no_digest, kHubExe);
    CHECK(r.has_value());
    if (r) CHECK(r->kind == Kind::None);

    std::string wrong_scheme = R"json({
      "tag_name": "v2.1.0",
      "assets": [{"name": "GameLoopTunerSetup.exe",
                  "browser_download_url": "https://x/setup.exe",
                  "digest": "md5:abcd"}]
    })json";
    const auto r2 = parse_latest_json(wrong_scheme, kHubExe);
    CHECK(r2.has_value());
    if (r2) CHECK(r2->kind == Kind::None);
}

GLT_TEST(UpdateJson, refuses_unusable_bodies) {
    std::string no_asset = R"json({
      "tag_name": "v2.1.0",
      "assets": [{"name": "README.txt", "digest": "sha256:00"}]
    })json";
    const auto none = parse_latest_json(no_asset, kBetaExe);
    CHECK(none.has_value());
    if (none) CHECK(none->kind == Kind::None);

    std::string no_assets_key = R"json({"tag_name": "v2.1.0"})json";
    CHECK(!parse_latest_json(no_assets_key, kBetaExe).has_value());

    std::string no_tag = R"json({"assets": []})json";
    CHECK(!parse_latest_json(no_tag, kBetaExe).has_value());

    CHECK(!parse_latest_json("not json at all", kBetaExe).has_value());
    CHECK(!parse_latest_json("", kBetaExe).has_value());
    CHECK(!parse_latest_json("[1,2,3]", kBetaExe).has_value());
}

// ── SHA-256 ──────────────────────────────────────────────────────────────────

namespace {

// A scratch file in %TEMP%; fixed name because ctest runs the suite serially.
std::wstring scratch_path(const char* name) {
    wchar_t dir[MAX_PATH];
    const DWORD n = ::GetTempPathW(MAX_PATH, dir);
    if (n == 0 || n >= MAX_PATH) return {};
    return glt::str::path_join(std::wstring(dir, n), glt::str::to_wide(name));
}

} // namespace

GLT_TEST(UpdateSha256, matches_known_vectors) {
    // FIPS 180-4 vectors for "abc" and the empty string. If these ever fail,
    // the hashing layer (not the test) is wrong.
    const std::wstring abc = scratch_path("glt_sha_abc.bin");
    CHECK(glt::str::write_file(abc, "abc"));
    CHECK_EQ(sha256_file(abc),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    const std::wstring empty = scratch_path("glt_sha_empty.bin");
    CHECK(glt::str::write_file(empty, ""));
    CHECK_EQ(sha256_file(empty),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    // >64 KiB so the 64 KiB streaming path is actually exercised.
    std::string big(200000, 'x');
    const std::wstring bigp = scratch_path("glt_sha_big.bin");
    CHECK(glt::str::write_file(bigp, big));
    CHECK_EQ(sha256_file(bigp).size(), std::size_t{64}); // well-formed hex
    CHECK_EQ(sha256_file(bigp), sha256_file(bigp));      // deterministic

    CHECK(glt::str::remove_file(abc));
    CHECK(glt::str::remove_file(empty));
    CHECK(glt::str::remove_file(bigp));
}

GLT_TEST(UpdateSha256, missing_file_returns_empty) {
    CHECK_EQ(sha256_file(scratch_path("glt_sha_does_not_exist.bin")), std::string());
}
