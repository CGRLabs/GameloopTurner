"""HKCU GameLoop settings. QWord-aware. Read-only helpers + guarded writes."""
import winreg

BASE = r'Software\Tencent\GameLoop'
COMMON = BASE + r'\Settings\Common'


def _open(path: str, write: bool = False):
    access = winreg.KEY_READ if not write else winreg.KEY_READ | winreg.KEY_SET_VALUE
    return winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, access)


def read_all(path: str) -> dict:
    out = {}
    try:
        with _open(path) as k:
            i = 0
            while True:
                try:
                    name, val, _typ = winreg.EnumValue(k, i)
                    out[name] = val
                    i += 1
                except OSError:
                    break
    except FileNotFoundError:
        pass
    return out


def subkeys(path: str) -> list:
    out = []
    try:
        with _open(path) as k:
            i = 0
            while True:
                try:
                    out.append(winreg.EnumKey(k, i))
                    i += 1
                except OSError:
                    break
    except FileNotFoundError:
        pass
    return out


def perf_allocation() -> dict:
    return {k: read_all(COMMON).get(k) for k in
            ('PerformanceMode', 'PerformanceMemoryGb', 'PerformanceCpuCoreNum',
             'RenderMode', 'RenderUserType')}


# RenderMode = "Screen rendering mode" (GameLoop Engine settings). Undocumented
# numeric enum; the UI offers Auto / OpenGL+ / DirectX+ (newer builds add Vulkan).
# 0 = Auto -> the engine picks the backend per launched game. We only DISPLAY it:
# writing it would fight the engine's per-map auto-selection, and RenderUserType
# is the "user overrode renderer" marker (0 = never touched).
RENDER_MODE_LABELS = {
    0: 'Auto (engine picks per game)',
    1: 'OpenGL+',
    2: 'DirectX+',
    3: 'Vulkan',
}
RENDER_USER_LABELS = {0: 'Auto (never overridden)', 1: 'Manually overridden'}


def render_mode_label(v) -> str:
    try:
        return RENDER_MODE_LABELS.get(int(v), 'unknown (%s)' % v)
    except (TypeError, ValueError):
        return str(v)


def render_user_label(v) -> str:
    try:
        return RENDER_USER_LABELS.get(int(v), 'unknown (%s)' % v)
    except (TypeError, ValueError):
        return str(v)


def set_perf(cores: int, mem_gb: int, host_cores: int = 0, host_mem_gb: int = 0) -> list:
    notes = []
    if host_cores and cores > host_cores - 1:
        raise ValueError('cores %d leaves no CPU for Windows (host %d)' % (cores, host_cores))
    if host_mem_gb and mem_gb > host_mem_gb - 2:
        raise ValueError('mem %dGB leaves no RAM for Windows (host %dGB)' % (mem_gb, host_mem_gb))
    with _open(COMMON, write=True) as k:
        for name, val in (('PerformanceCpuCoreNum', cores), ('PerformanceMemoryGb', mem_gb)):
            winreg.SetValueEx(k, name, 0, winreg.REG_QWORD, int(val))
            notes.append('%s=%d' % (name, val))
    return notes


def export_branch(path: str) -> dict:
    """Recursive {path: {values}} for backup."""
    out = {path: read_all(path)}
    for sub in subkeys(path):
        out.update(export_branch(path + '\\' + sub))
    return out
