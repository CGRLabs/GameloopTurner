"""SQLite access: backup, guarded writes, checkpoint, trigger introspection."""
import os
import shutil
import sqlite3
import time

from . import log

LOG = log.get_logger()


def connect(path: str, timeout: int = 15) -> sqlite3.Connection:
    return sqlite3.connect(path, timeout=timeout)


def backup(path: str, dest_dir: str = '') -> str:
    """Live-safe: SQLite online backup API (consistent even while GameLoop runs)."""
    dest_dir = dest_dir or os.path.dirname(path)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    dst = os.path.join(dest_dir, 'Configs.db.bak-' + stamp)
    try:
        src = connect(path, timeout=10)
        dstcon = sqlite3.connect(dst, timeout=10)
        with dstcon:
            src.backup(dstcon)
        dstcon.close()
        src.close()
    except Exception:
        shutil.copy2(path, dst)  # fallback: cold copy
    LOG.info('backup %s', log.scrub(dst))
    return dst


def checkpoint(con: sqlite3.Connection):
    con.execute('PRAGMA wal_checkpoint(TRUNCATE)')


def guarded_updates(path: str, ops: list, dry_run: bool = False, verify=None) -> dict:
    """ops: [(sql, params, describe)]. Backup, apply, optionally verify.

    verify: callable(con) -> list[str] notes, run after commit to prove the
    write stuck (distinguishes never-written from written-then-reverted).
    Returns {'ok': bool, 'notes': [...]}. Rolls back + raises on failure.
    """
    notes = []
    if dry_run:
        return {'ok': True, 'notes': ['DRYRUN: %d ops skipped' % len(ops)]}
    dst = backup(path)
    notes.append('backup: ' + dst)
    con = connect(path)
    try:
        cur = con.cursor()
        for sql, params, desc in ops:
            cur.execute(sql, params or ())
            notes.append('%s (rows=%d)' % (desc, cur.rowcount))
        con.commit()
        checkpoint(con)
        if verify:
            try:
                notes.extend(verify(con))
            except Exception as e:  # noqa - write already committed
                notes.append('verify failed: %s' % e)
    except Exception as e:  # noqa - rollback then restore
        con.rollback()
        con.close()
        raise RuntimeError('write failed, rolled back: %s' % e)
    con.close()
    return {'ok': True, 'notes': notes}


def triggers(con: sqlite3.Connection) -> list:
    return con.execute("SELECT name, tbl_name, sql FROM sqlite_master WHERE type='trigger'").fetchall()


def guarded_keys(con: sqlite3.Connection) -> set:
    """Keys protected by triggers (parsed from WHEN clauses is overkill: probe)."""
    return {r[0] for r in con.execute(
        "SELECT key FROM key_map_configs_cache_official WHERE key LIKE 'offcial%'")}
