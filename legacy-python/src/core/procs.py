"""Process + service handling via stdlib only (tasklist/sc/wmic-free where possible)."""
import csv
import io
import subprocess
import time

NAMES = ('GameLoop', 'GameLoopEmulator', 'GameLoopVm', 'GameLoopAssistant', 'GameLoopDldSvr')
SERVICE = 'GameLoopService'
SERVICE_EXE = SERVICE.lower() + '.exe'

# Force-kill order: game first, UI last. The service is never in this list.
KILL_ORDER = ('gameloopvm.exe', 'gameloopemulator.exe',
              'gameloopassistant.exe', 'gameloopassistanttoast.exe',
              'gameloopdldsvr.exe', 'gameloopvfs.exe', 'gameloopupdate.exe',
              'gameloopinstaller.exe', 'cefrendererprocess.exe',
              'gameloop.exe', 'gamelooplauncher.exe')


def _run(args: list, timeout: int = 30) -> str:
    try:
        out = subprocess.run(args, capture_output=True, text=True, timeout=timeout,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        return out.stdout or ''
    except Exception:
        return ''


def list_gameloop() -> list:
    """[{'name','pid','mem_kb'}] for GameLoop* processes (excludes the tuner itself)."""
    out = _run(['tasklist', '/FO', 'CSV', '/NH'])
    rows = []
    try:
        for r in csv.reader(io.StringIO(out)):
            if len(r) >= 5 and r[0].lower().startswith('gameloop') and 'tuner' not in r[0].lower():
                mem = r[4].replace(',', '').replace(' K', '').strip()
                rows.append({'name': r[0], 'pid': int(r[1]), 'mem_kb': int(mem) if mem.isdigit() else 0})
    except Exception:
        pass
    return rows


def running(exclude_service: bool = True) -> list:
    """GameLoop processes, service excluded by default."""
    rows = list_gameloop()
    if exclude_service:
        rows = [p for p in rows if p['name'].lower() != SERVICE_EXE]
    return rows


def service_status() -> dict:
    out = _run(['sc', 'query', SERVICE])
    state = 'UNKNOWN'
    for line in out.splitlines():
        if 'STATE' in line:
            state = line.strip()
    start = _run(['sc', 'qc', SERVICE])
    mode = 'UNKNOWN'
    for line in start.splitlines():
        if 'START_TYPE' in line:
            mode = line.strip()
    return {'state': state, 'start_type': mode}


def graceful_close(wait_s: int = 8) -> list:
    """Ask windows to close (works without admin). Returns leftovers.

    EnumWindows enumerates hidden/tray top-level windows too, so this reaches
    the minimized-to-tray UI. If the UI is running elevated (mid-match, spawned
    by the LocalSystem service) UIPI silently drops WM_CLOSE -> the caller's
    ladder escalates to taskkill.
    """
    import ctypes
    WM_CLOSE = 0x0010
    try:
        EnumWindows = ctypes.windll.user32.EnumWindows
        GetWindowThreadProcessId = ctypes.windll.user32.GetWindowThreadProcessId
        PostMessageW = ctypes.windll.user32.PostMessageW
    except Exception:
        return running()
    pids = {p['pid'] for p in running()}
    hit = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(hwnd, _):
        pid = ctypes.c_ulong()
        GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids:
            hit.append(hwnd)
        return True
    try:
        EnumWindows(cb, None)
        for h in hit:
            PostMessageW(h, WM_CLOSE, 0, 0)
    except Exception:
        pass
    time.sleep(wait_s)
    return running()


def _enable_debug_privilege() -> bool:
    """SeDebugPrivilege lets an elevated Tuner terminate SYSTEM/elevated procs."""
    import ctypes
    from ctypes import wintypes
    try:
        advapi = ctypes.windll.advapi32
        k32 = ctypes.windll.kernel32
        TOKEN_ADJUST_PRIVILEGES, TOKEN_QUERY = 0x20, 0x08
        SE_PRIVILEGE_ENABLED = 0x02
        tok = wintypes.HANDLE()
        if not advapi.OpenProcessToken(k32.GetCurrentProcess(),
                                       TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                       ctypes.byref(tok)):
            return False
        luid = wintypes.LUID()
        if not advapi.LookupPrivilegeValueW(None, 'SeDebugPrivilege', ctypes.byref(luid)):
            k32.CloseHandle(tok)
            return False

        class TP(ctypes.Structure):
            _fields_ = [('PrivilegeCount', wintypes.DWORD),
                        ('Privileges', wintypes.LUID_AND_ATTRIBUTES * 1)]
        tp = TP(1, (wintypes.LUID_AND_ATTRIBUTES * 1)(wintypes.LUID_AND_ATTRIBUTES(luid, SE_PRIVILEGE_ENABLED)))
        ok = advapi.AdjustTokenPrivileges(tok, False, ctypes.byref(tp), 0, None, None)
        k32.CloseHandle(tok)
        return bool(ok)
    except Exception:
        return False


def _order_key(name: str) -> int:
    n = (name or '').lower()
    return KILL_ORDER.index(n) if n in KILL_ORDER else len(KILL_ORDER)


def _kill_pids(pids: list, force: bool = False, wait: int = 3) -> list:
    """taskkill the given PIDs in group order (/T catches child procs)."""
    if not pids:
        return running()
    names = {p['pid']: p['name'] for p in list_gameloop()}
    for pid in sorted(set(pids), key=lambda x: _order_key(names.get(x, ''))):
        args = ['taskkill', '/PID', str(pid), '/T']
        if force:
            args.append('/F')
        _run(args, timeout=15)
    time.sleep(wait)
    return running()


def _tray_quit_fallback(wait_s: int = 2) -> list:
    """Last resort: post WM_QUIT to every UI thread of the GameLoop procs.

    A true tray right-click automation needs UI automation (fragile across
    Win10/11 overflow behaviour); WM_QUIT reaches the Qt message loop and is
    the closest reliable programmatic 'Quit'. Returns notes.
    """
    import ctypes
    notes = []
    WM_QUIT = 0x0012
    try:
        EnumWindows = ctypes.windll.user32.EnumWindows
        GetWindowThreadProcessId = ctypes.windll.user32.GetWindowThreadProcessId
        PostThreadMessageW = ctypes.windll.user32.PostThreadMessageW
        pids = {p['pid'] for p in running()}
        threads = set()

        @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        def cb(hwnd, _):
            pid = ctypes.c_ulong()
            tid = GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value in pids and tid:
                threads.add(tid)
            return True
        EnumWindows(cb, None)
        for tid in threads:
            PostThreadMessageW(tid, WM_QUIT, 0, 0)
        notes.append('tray-quit fallback: WM_QUIT -> %d UI thread(s)' % len(threads))
    except Exception as e:  # noqa
        notes.append('tray-quit fallback failed: %s' % e)
    time.sleep(wait_s)
    return notes


def stop_service(wait_s: int = 4) -> str:
    """Stop GameLoopService (admin). Auto-start stays, so it returns on reboot."""
    _run(['sc', 'stop', SERVICE], timeout=20)
    time.sleep(wait_s)
    return 'service stop -> %s' % service_status().get('state')


def close_gameloop(grace_wait: int = 6, kill_wait: int = 3,
                   force: bool = True, stop_service_now: bool = False) -> dict:
    """Dedicated close ladder honouring the elevated-mid-match reality.

    graceful WM_CLOSE -> taskkill /T -> taskkill /F /T -> WM_QUIT fallback.
    Returns {'ok', 'notes', 'left'}.
    """
    _enable_debug_privilege()
    notes = []
    live = running()
    if not live:
        return {'ok': True, 'notes': ['GameLoop already closed'], 'left': []}
    notes.append('closing %d GameLoop process(es)' % len(live))
    left = graceful_close(grace_wait)
    notes.append('graceful WM_CLOSE: %d left' % len(left))
    if left:
        left = _kill_pids([p['pid'] for p in left], force=False, wait=kill_wait)
        notes.append('taskkill /T: %d left' % len(left))
    if left and force:
        left = _kill_pids([p['pid'] for p in left], force=True, wait=kill_wait)
        notes.append('taskkill /F /T: %d left' % len(left))
    if left:
        notes += _tray_quit_fallback()
        left = running()
    if stop_service_now:
        notes.append(stop_service())
        left = running()
    return {'ok': not left, 'notes': notes, 'left': left}


def host_resources() -> dict:
    """(cores, phys_gb) of THIS machine. Portable."""
    import ctypes
    import multiprocessing
    cores = multiprocessing.cpu_count()
    mem_gb = 0
    try:
        class MS(ctypes.Structure):
            _fields_ = [('wLength', ctypes.c_ulong), ('wMemoryLoad', ctypes.c_ulong),
                        ('ullTotalPhys', ctypes.c_ulonglong), ('ullAvailPhys', ctypes.c_ulonglong),
                        ('ullTotalPageFile', ctypes.c_ulonglong), ('ullAvailPageFile', ctypes.c_ulonglong),
                        ('ullTotalVirtual', ctypes.c_ulonglong), ('ullAvailVirtual', ctypes.c_ulonglong),
                        ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]
        ms = MS()
        ms.wLength = ctypes.sizeof(MS)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms)):
            mem_gb = int(ms.ullTotalPhys // (1024 ** 3))
    except Exception:
        pass
    return {'cores': cores, 'mem_gb': mem_gb}


PRIO = {'high': 0x80, 'above': 0x8000, 'normal': 0x20, 'below': 0x4000}


def set_priorities(plan: dict = None) -> list:
    """plan: {exe_lower: prio_name}. Own-user processes need no admin. Returns notes."""
    import ctypes
    plan = plan or {'gameloopemulator.exe': 'high', 'gameloopvm.exe': 'high',
                    'gameloop.exe': 'high', 'gameloopassistant.exe': 'above',
                    'gameloopdldsvr.exe': 'below'}
    notes = []
    try:
        k32 = ctypes.windll.kernel32
    except Exception:
        return ['priority: win32 API unavailable']
    for p in list_gameloop():
        want = plan.get(p['name'].lower())
        if not want:
            continue
        try:
            h = k32.OpenProcess(0x0200 | 0x0400, False, p['pid'])  # QUERY_INFO|SET_INFO
            if not h:
                notes.append('%s(pid %d): open denied (elevated?)' % (p['name'], p['pid']))
                continue
            ok = k32.SetPriorityClass(h, PRIO[want])
            k32.CloseHandle(h)
            notes.append('%s(pid %d): %s %s' % (p['name'], p['pid'], want, 'ok' if ok else 'fail'))
        except Exception as e:  # noqa
            notes.append('%s(pid %d): %s' % (p['name'], p['pid'], e))
    if not notes:
        notes.append('no GameLoop processes running')
    return notes
