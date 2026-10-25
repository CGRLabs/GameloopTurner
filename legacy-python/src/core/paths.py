"""Portable path discovery. No hardcoded users, drives, or versions."""
import os
import glob as _glob

APP = 'GameLoop'
PKG = 'com.activision.callofduty.shooter'


def appdata_db() -> str:
    p = os.path.join(os.environ.get('APPDATA', ''), 'Tencent', 'GameLoop', 'db', 'Configs.db')
    return p if os.path.isfile(p) else ''


def smk_conf() -> str:
    p = os.path.join(os.environ.get('APPDATA', ''), 'Tencent', 'GameLoop', 'config', PKG, 'smk.conf')
    return p if os.path.isfile(p) else ''


def install_root() -> str:
    """Find D:/C:/.../Tencent/GameLoop on any fixed drive."""
    cands = []
    for var in ('ProgramFiles', 'ProgramFiles(x86)', 'ProgramW6432'):
        base = os.environ.get(var, '')
        if base:
            cands.append(os.path.join(base, 'Tencent', 'GameLoop'))
    try:
        import string
        from ctypes import windll  # noqa
        bitmask = windll.kernel32.GetLogicalDrives()
        for i in string.ascii_uppercase:
            if bitmask & 1:
                for tail in (r'Program Files\Tencent\GameLoop', r'Tencent\GameLoop'):
                    cands.append(os.path.join(i + ':\\', tail))
            bitmask >>= 1
    except Exception:
        for d in ('C:\\', 'D:\\', 'E:\\'):
            cands.append(os.path.join(d, 'Program Files', 'Tencent', 'GameLoop'))
    for c in cands:
        if os.path.isdir(c):
            return c
    return ''


def app_version_dir(root: str = '') -> str:
    """Newest Application/<ver> dir (Application root itself has only stubs)."""
    root = root or install_root()
    if not root:
        return ''
    cands = sorted(_glob.glob(os.path.join(root, 'Application', '*.*.*')))
    cands = [c for c in cands if os.path.isdir(c)]
    return cands[-1] if cands else ''


def exe_in_build(name: str, root: str = '') -> str:
    d = app_version_dir(root)
    if not d:
        return ''
    p = os.path.join(d, name)
    return p if os.path.isfile(p) else ''


def adb_exe(root: str = '') -> str:
    return exe_in_build('adb.exe', root)


def host_info() -> dict:
    import platform
    return {'machine': platform.machine(), 'release': platform.release(),
            'version': platform.version()}
