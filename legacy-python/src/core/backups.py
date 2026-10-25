"""Backup listing + one-click restore (Configs.db, smk.conf, registry export)."""
import glob as _glob
import json
import os
import shutil


def list_backups(db_path: str) -> list:
    d = os.path.dirname(db_path)
    out = []
    for p in _glob.glob(os.path.join(d, 'Configs.db*.bak-*')) + \
            _glob.glob(os.path.join(d, 'smk.conf.bak-*')):
        try:
            out.append({'path': p, 'bytes': os.path.getsize(p)})
        except OSError:
            pass
    return sorted(out, key=lambda r: r['path'])


def restore_db(db_path: str, bak_path: str) -> list:
    """GameLoop must be closed first (caller enforces). Drops -wal/-shm to rebuild."""
    notes = []
    shutil.copy2(bak_path, db_path)
    notes.append('restored %s' % os.path.basename(bak_path))
    for ext in ('-wal', '-shm'):
        try:
            os.remove(db_path + ext)
            notes.append('dropped %s (rebuilds on launch)' % ext)
        except OSError:
            pass
    return notes


def save_reg_snapshot(path: str, data: dict) -> str:
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1)
    return path
