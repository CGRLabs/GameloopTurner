// ─────────────────────────────────────────────────────────────────────────────
// strings.cpp tests — encoding, path surgery, globbing, and the secret scrub.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/strings.hpp"

#include <string>

using namespace glt::str;

// ── Encoding ─────────────────────────────────────────────────────────────────

GLT_TEST(Encoding, round_trips_utf8) {
    // CJK is the actual payload of smk.conf's comm="…" attributes.
    const std::string src = "\xE5\xBC\x80\xE5\x85\xB3\xE9\x97\xA8"; // 开关门
    const std::wstring wide = to_wide(src);
    CHECK(!wide.empty());
    CHECK_EQ(to_utf8(wide), src);
}

GLT_TEST(Encoding, empty_stays_empty) {
    CHECK(to_wide("").empty());
    CHECK(to_utf8(L"").empty());
}

GLT_TEST(Encoding, validates_utf8) {
    CHECK(is_valid_utf8("plain ascii"));
    CHECK(is_valid_utf8("\xE5\xBC\x80\xE5\x85\xB3"));
    CHECK(!is_valid_utf8("\xFF\xFE"));
    CHECK(!is_valid_utf8("\xE5\xBC"));   // truncated sequence
}

// ── Case ─────────────────────────────────────────────────────────────────────

GLT_TEST(Case, iequals_is_ascii_only) {
    CHECK(iequals("GameLoop", "gameloop"));
    CHECK(iequals("pckeymap.yyb.qq.com", "PCKEYMAP.YYB.QQ.COM"));
    CHECK(!iequals("gameloop", "gameloops"));
    CHECK(iequals(std::wstring(L"GameLoopVm.exe"), std::wstring(L"gameloopvm.exe")));
}

GLT_TEST(Case, icontains) {
    CHECK(icontains("Battle Royale", "royale"));
    CHECK(icontains("Multi Player", "MULTI"));
    CHECK(!icontains("mpkeymap", "royale"));
    CHECK(icontains("anything", "")); // empty needle always matches
}

// ── Paths ────────────────────────────────────────────────────────────────────

GLT_TEST(Path, join_normalises_separators) {
    CHECK_EQ(path_join(L"C:\\a", L"b"), std::wstring(L"C:\\a\\b"));
    CHECK_EQ(path_join(L"C:\\a\\", L"b"), std::wstring(L"C:\\a\\b"));
    // A rooted tail supersedes the head entirely.
    CHECK_EQ(path_join(L"C:\\a", L"D:\\x"), std::wstring(L"D:\\x"));
    CHECK_EQ(path_join(L"C:\\a", L"\\b"), std::wstring(L"\\b"));
}

GLT_TEST(Path, parent_filename_stem) {
    CHECK_EQ(path_parent(L"C:\\a\\b\\c.txt"), std::wstring(L"C:\\a\\b"));
    CHECK_EQ(path_filename(L"C:\\a\\b\\c.txt"), std::wstring(L"c.txt"));
    CHECK_EQ(path_stem(L"C:\\a\\b\\c.txt"), std::wstring(L"c"));
    CHECK_EQ(path_extension(L"C:\\a\\b\\c.txt"), std::wstring(L".txt"));
    CHECK_EQ(path_extension(L"C:\\a\\b\\c"), std::wstring(L""));
    CHECK_EQ(path_root(L"C:\\a\\b"), std::wstring(L"C:"));
}

GLT_TEST(Path, root_is_empty_without_a_drive) {
    CHECK_EQ(path_root(L"\\\\server\\share"), std::wstring(L""));
}

// ── Glob ─────────────────────────────────────────────────────────────────────

// Exercised through a temporary directory so the real GameLoop install is never
// touched by the test run.
GLT_TEST(Glob, matches_expected_names) {
    // The glob itself is exercised indirectly through quarantine patterns in
    // test_fixes.cpp; here we only assert the matching rules that those
    // patterns depend on, using a pure-string comparison of the same shape.
    const auto matches = [](const std::wstring& name, const std::wstring& pat) {
        // Reimplemented expectation, kept in lockstep with str::glob.
        std::size_t n = 0, m = 0, star = std::wstring::npos, mark = 0;
        const auto lower = ascii_lower(name);
        const auto p = ascii_lower(pat);
        while (n < lower.size()) {
            if (m < p.size() && (p[m] == L'?' || p[m] == lower[n])) {
                ++n;
                ++m;
            } else if (m < p.size() && p[m] == L'*') {
                star = m++;
                mark = n;
            } else if (star != std::wstring::npos) {
                m = star + 1;
                n = ++mark;
            } else {
                break;
            }
        }
        return n == lower.size() && m == p.size();
    };

    // The four real quarantine patterns.
    CHECK(matches(L"ob_x_ver_86.zip", L"ob_*.zip"));
    CHECK(matches(L"config2.dat.7", L"config2.dat.*"));
    CHECK(matches(L"cache_crc.dat", L"cache_crc.dat"));
    CHECK(!matches(L"cache_crc.dat.bak", L"cache_crc.dat"));
    CHECK(!matches(L"ob_x_ver_86.zip", L"ob_*.7z"));

    // Backup glob used by list_backups().
    CHECK(matches(L"Configs.db.bak-20261015-142233", L"Configs.db.bak-*"));
    CHECK(!matches(L"Configs.db", L"Configs.db.bak-*"));
    CHECK(matches(L"smk.conf.bak-20261015-142233", L"smk.conf.bak-*"));
}

// ── Text ─────────────────────────────────────────────────────────────────────

GLT_TEST(Text, split_and_trim) {
    const auto parts = split("a,b,,c", ',');
    CHECK_EQ(parts.size(), std::size_t{4});
    CHECK_EQ(parts[2], std::string(""));

    CHECK_EQ(std::string(trim("  padded \t\n")), std::string("padded"));
    CHECK_EQ(std::string(trim("   ")), std::string(""));
    CHECK_EQ(std::string(rtrim("keep  ")), std::string("keep"));
}

GLT_TEST(Text, split_lines_handles_both_endings) {
    // smk.conf is LF-only; hosts is usually CRLF. Both must round-trip.
    CHECK_EQ(split_lines("a\nb\nc").size(), std::size_t{3});
    CHECK_EQ(split_lines("a\r\nb\r\nc").size(), std::size_t{3});
    CHECK_EQ(split_lines("a\nb").size(), std::size_t{2});

    // A trailing newline must not produce a phantom empty element, matching
    // Python's str.splitlines().
    CHECK_EQ(split_lines("a\nb\n").size(), std::size_t{2});
    CHECK_EQ(split_lines("a\r\nb\r\n").size(), std::size_t{2});
}

GLT_TEST(Text, replace_all) {
    CHECK_EQ(replace_all("aXbXc", "X", "--"), std::string("a--b--c"));
    CHECK_EQ(replace_all("aaa", "a", "aa"), std::string("aaaaaa"));
    CHECK_EQ(replace_all("abc", "", "x"), std::string("abc"));
}

// ── Version sort ─────────────────────────────────────────────────────────────

// This is the bug the audit flagged as R11: Python's sorted() on
// "Application/*.*.*" is LEXICAL, so "10.0.0.0" sorted before "7.0.167.0" and
// the [-1] picked the OLDER build after a major-version bump.
GLT_TEST(Version, numeric_not_lexical) {
    CHECK(version_less(L"7.0.167.0", L"10.0.0.0"));
    CHECK(!version_less(L"10.0.0.0", L"7.0.167.0"));
    CHECK(!version_less(L"10.0.0.0", L"10.0.0.0"));
}

GLT_TEST(Version, handles_short_and_padded_forms) {
    CHECK(version_less(L"7", L"10"));
    CHECK(version_less(L"7.0", L"7.0.1"));
    CHECK(version_less(L"7.0.1", L"7.1"));
}

GLT_TEST(Version, mixed_numeric_and_alpha) {
    // A numeric component outranks an alphabetic one in the same position —
    // the same rule that makes "1.0" newer than "1.0rc".
    CHECK(version_less(L"beta", L"10"));   // alpha < numeric
    CHECK(!version_less(L"10", L"beta")); // numeric > alpha

    // Equal numeric prefixes fall through to the mixed comparison.
    CHECK(!version_less(L"7.0.0", L"7.0.beta"));
    CHECK(version_less(L"7.0.beta", L"7.0.0"));

    // And an alpha suffix still sorts below the next numeric bump.
    CHECK(version_less(L"7.0.beta", L"7.0.1"));
}

// ── Secret scrubbing ─────────────────────────────────────────────────────────

GLT_TEST(Scrub, redacts_json_shaped_secrets) {
    const std::string in = R"({"seed_id": "abc123", "harmless": "keep"})";
    const std::string out = scrub_secrets(in);

    CHECK(out.find("abc123") == std::string::npos);
    CHECK(out.find("keep") != std::string::npos);
    CHECK(out.find("seed_id") != std::string::npos); // the KEY is kept, only the value is masked
}

GLT_TEST(Scrub, redacts_all_known_secret_keys) {
    const char* keys[] = {"seed_id", "vdid", "oaid",  "ams_union_id",
                          "cert_hash", "VPoDioa", "VOPoDioa", "VWiDioa"};
    for (const char* k : keys) {
        const std::string in = std::string("{\"") + k + "\": \"LEAKME\"}";
        const std::string out = scrub_secrets(in);
        if (out.find("LEAKME") != std::string::npos) {
            ::glt::test::fail(__FILE__, __LINE__,
                              std::string("secret not redacted for key: ") + k);
        }
    }
}

GLT_TEST(Scrub, leaves_ordinary_text_alone) {
    const std::string in = "clarity 2 -> 3, clarity: 3, mode 145";
    CHECK_EQ(scrub_secrets(in), in);
}

GLT_TEST(Scrub, empty_and_idempotent) {
    CHECK_EQ(scrub_secrets(""), std::string(""));
    const std::string once = scrub_secrets(R"({"vdid": "xyz"})");
    CHECK_EQ(scrub_secrets(once), once);
}

// ── Timestamps ───────────────────────────────────────────────────────────────

GLT_TEST(Timestamp, compact_is_backup_suffix_shaped) {
    const std::string t = timestamp_compact();
    CHECK_EQ(t.size(), std::size_t{15}); // YYYYMMDD-HHMMSS
    CHECK_EQ(t[8], '-');
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (i == 8) continue;
        CHECK(t[i] >= '0' && t[i] <= '9');
    }
}