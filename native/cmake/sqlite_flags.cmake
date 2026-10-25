# ─────────────────────────────────────────────────────────────────────────────
# SQLite compile flags — aggressively trimmed for binary size.
#
# The amalgamation is ~8 MB of source and compiles to ~1.3 MB of object code
# with default settings, which alone would blow the ~1.5 MB budget. Everything
# below is a feature this application provably never touches.
# ─────────────────────────────────────────────────────────────────────────────

# THREADSAFE=1 (serialized) rather than 0. The drift monitor thread and the
# Apply All worker both open the same Configs.db. Serialized mode costs ~40 KB
# but removes an entire class of data races that a shared mutex would not
# fully cover anyway (sqlite3_backup_step yields internally).
set(SQLITE_THREADSAFE 1)

set(GLT_SQLITE_DEFS
    SQLITE_THREADSAFE=1

    # ── Dynamic loading ──────────────────────────────────────────────────
    # load_extension() is the classic malware primitive. The tuner never loads
    # an extension; dropping it removes a DLL-loading code path entirely.
    SQLITE_OMIT_LOAD_EXTENSION

    # ── Legacy surface ───────────────────────────────────────────────────
    # Deprecated APIs (sqlite3_aggregate_count, MEMORY_*, sqlite3_get_table)
    # are dead weight we never reach.
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_EXPLAIN
    SQLITE_OMIT_TRACE
    SQLITE_OMIT_COMPILEOPTION_DIAGS
    SQLITE_OMIT_GET_TABLE

    # ── Features we never use ────────────────────────────────────────────
    # Queries are single-table or key lookups; we never share a cache.
    #
    # SQLITE_OMIT_ATTACH is deliberately NOT set. It looks like a free win but
    # is not a supported option: it removes the BODY of sqlite3DbIsNamed()
    # while leaving its forward declaration in place, and the amalgamation then
    # fails to compile with
    #   C2129: static function 'sqlite3DbIsNamed' declared but not defined
    # It is not in the documented OMIT list for that reason.
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_OMIT_PROGRESS_CALLBACK
    SQLITE_OMIT_COMPLETE
    SQLITE_OMIT_TCL_VARIABLE
    SQLITE_OMIT_UTF16

    # ── Second tier ──────────────────────────────────────────────────────
    # Everything below removes a feature this application provably never
    # touches. Each was checked against the query set in src/core/keymaps.cpp
    # and src/core/db.cpp, which is SELECT / INSERT OR REPLACE / UPDATE /
    # DELETE against two key/value tables.
    #
    # SEVEN options look free but are NOT supported by the amalgamation —
    # each one deletes a function BODY while leaving its forward declaration
    # behind, and the build dies with C2129 "declared but not defined":
    #
    #   SQLITE_OMIT_ATTACH       → sqlite3DbIsNamed
    #   SQLITE_OMIT_ALTERTABLE   → sqlite3AlterRenameColumn
    #   SQLITE_OMIT_VIRTUALTABLE → sqlite3AlterRenameColumn
    #   SQLITE_OMIT_REINDEX      → sqlite3Reindex
    #
    # (and the ANALYZE / DECLTYPE / DESERIALIZE family behaves the same way,
    # dropping sqlite3_analyzer / sqlite3_column_decltype / …). They are absent
    # from the documented OMIT list for that reason.
    #
    # The virtual-table code is the single biggest remaining chunk in .text. If
    # the budget ever demands it, the supported route is patching the
    # amalgamation — not setting these defines.
    SQLITE_OMIT_AUTOINCREMENT   # no AUTOINCREMENT rows exist
    SQLITE_OMIT_INCRBLOB        # incremental blob I/O API

    # ── Safety ───────────────────────────────────────────────────────────
    # DQS=0: reject double-quoted string literals outright. Every query the
    # tuner issues uses proper single-quoted bindings or named params, so this
    # closes a real injection surface at zero cost to us.
    SQLITE_DQS=0

    # LIKE on BLOBs is meaningless for our key/value tables.
    SQLITE_LIKE_DOESNT_MATCH_BLOBS

    # ── Memory / build ───────────────────────────────────────────────────
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DEFAULT_FOREIGN_KEYS=0
    SQLITE_USE_ALLOCA
    # Expression-depth checking walks every node of every expression to enforce
    # a recursion limit. We own every statement we run (they are built from
    # fixed templates plus bound parameters), so the walk is pure overhead.
    SQLITE_MAX_EXPR_DEPTH=0
)

if(MSVC)
    # The amalgamation trips these on its own macros; they are not ours to fix.
    list(APPEND GLT_SQLITE_DEFS
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE)
endif()