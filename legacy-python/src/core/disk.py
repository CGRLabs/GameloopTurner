"""Disk footprint + free-space gauge."""
import os
import shutil

ENGINE_MIN_FREE_GB = 10.0


def _du(path: str) -> int:
    total = 0
    for root, _dirs, files in os.walk(path):
        for f in files:
            try:
                total += os.path.getsize(os.path.join(root, f))
            except OSError:
                pass
    return total


def footprint(install_root: str) -> dict:
    out = {}
    if not install_root:
        return out
    for sub in ('', 'GameLoop', 'GameLoopData'):
        p = os.path.join(install_root, sub) if sub else install_root
        if os.path.isdir(p):
            out[sub or 'Tencent'] = _du(p)
    appdata = os.path.join(os.environ.get('APPDATA', ''), 'Tencent')
    if os.path.isdir(appdata):
        out['AppData/Tencent'] = _du(appdata)
    return out


def drives() -> list:
    out = []
    for d in ('C:\\', 'D:\\', 'E:\\', 'F:\\'):
        if os.path.isdir(d):
            try:
                t, u, f = shutil.disk_usage(d)
                out.append({'drive': d, 'total_gb': t / 1e9, 'free_gb': f / 1e9})
            except OSError:
                pass
    return out


def game_drive_status(install_root: str) -> dict:
    drive = os.path.splitdrive(install_root or os.environ.get('APPDATA', ''))[0] + '\\'
    try:
        _t, _u, f = shutil.disk_usage(drive)
        free = f / 1e9
        return {'drive': drive, 'free_gb': round(free, 1),
                'ok': free >= ENGINE_MIN_FREE_GB, 'min_gb': ENGINE_MIN_FREE_GB}
    except OSError:
        return {'drive': drive, 'free_gb': -1, 'ok': False, 'min_gb': ENGINE_MIN_FREE_GB}
