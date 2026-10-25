"""ADB guest verify: proves allocation landed inside the VM. Read-only."""
import subprocess


def _run(adb: str, args: list, timeout: int = 30) -> str:
    try:
        out = subprocess.run([adb] + args, capture_output=True, text=True, timeout=timeout,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        return (out.stdout or '').strip()
    except Exception:
        return ''


def verify(adb: str, pkg: str) -> dict:
    if not adb:
        return {'ok': False, 'why': 'adb.exe not found'}
    out = {}
    _run(adb, ['connect', 'localhost:5555'])
    devs = _run(adb, ['devices'])
    if 'localhost:5555' not in devs or 'device' not in devs:
        return {'ok': False, 'why': 'emulator ADB not reachable (is it running?)'}
    sh = lambda c: _run(adb, ['-s', 'localhost:5555', 'shell', c])
    out['android'] = sh('getprop ro.build.version.release')
    out['model'] = sh('getprop ro.product.model')
    out['brand'] = sh('getprop ro.product.brand')
    try:
        out['guest_cpus'] = int(sh('nproc') or 0)
    except ValueError:
        out['guest_cpus'] = 0
    mem = sh('cat /proc/meminfo | head -1')
    try:
        out['guest_mem_gb'] = round(int(mem.split()[1]) / 1024 / 1024, 1)
    except Exception:
        out['guest_mem_gb'] = 0
    ver = sh('dumpsys package %s | grep versionName' % pkg)
    out['app_version'] = ver.split('=')[-1].strip() if '=' in ver else ver
    out['app_running'] = bool(sh('pidof %s' % pkg))
    pkgs = sh('pm list packages -3')
    out['third_party'] = sorted(p.split(':')[-1] for p in pkgs.splitlines() if p.strip()) if pkgs else []
    out['ok'] = True
    return out
