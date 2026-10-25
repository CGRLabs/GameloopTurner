"""Hosts-file block for the mode-map download host (durability experiment).

GameLoop re-downloads mode maps from ``pckeymap.yyb.qq.com`` every session
start (KeymapConfigManager::downloadModeMaps) and restores the blinded smk
templates, which re-arms the DMZ lobby jump. Blocking the host forces the
engine to fall back to its local DB/cache (which the Tuner blinds). Always
backed up and reversible via :func:`unblock`.
"""
import os
import shutil
import socket
import time

HOSTS = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'),
                     'System32', 'drivers', 'etc', 'hosts')
DOMAIN = 'pckeymap.yyb.qq.com'
MARK = '# gameloop-tuner: block mode-map download'


def _lines() -> list:
    try:
        with open(HOSTS, encoding='utf-8', errors='replace') as f:
            return f.read().splitlines()
    except OSError:
        return []


def is_blocked() -> bool:
    return any(DOMAIN in ln and '0.0.0.0' in ln for ln in _lines())


def _backup() -> str:
    bak = HOSTS + '.bak-' + time.strftime('%Y%m%d-%H%M%S')
    try:
        shutil.copy2(HOSTS, bak)
    except OSError:
        pass
    return bak


def block(dry: bool = False) -> list:
    if is_blocked():
        return ['hosts: %s already blocked' % DOMAIN]
    lines = _lines()
    lines += ['', '0.0.0.0 %s %s' % (DOMAIN, MARK)]
    if dry:
        return ['DRYRUN: would append "0.0.0.0 %s" to %s' % (DOMAIN, HOSTS)]
    bak = _backup()
    with open(HOSTS, 'w', encoding='utf-8', newline='') as f:
        f.write('\n'.join(lines) + '\n')
    return ['hosts backup: ' + bak, 'hosts: blocked %s (0.0.0.0)' % DOMAIN]


def unblock(dry: bool = False) -> list:
    lines = _lines()
    keep = [ln for ln in lines if DOMAIN not in ln and MARK not in ln]
    if len(keep) == len(lines):
        return ['hosts: no tuner block found']
    if dry:
        return ['DRYRUN: would remove %d hosts line(s)' % (len(lines) - len(keep))]
    bak = _backup()
    with open(HOSTS, 'w', encoding='utf-8', newline='') as f:
        f.write('\n'.join(keep) + '\n')
    return ['hosts backup: ' + bak, 'hosts: unblocked %s' % DOMAIN]


def probe() -> dict:
    """Resolve + TCP-connect the download host for the pre/post block test."""
    out = {'domain': DOMAIN, 'blocked': is_blocked(), 'resolved': [], 'tcp443': None}
    try:
        out['resolved'] = sorted({ai[4][0] for ai in socket.getaddrinfo(DOMAIN, 443)})
    except Exception as e:  # noqa
        out['error'] = str(e)
    try:
        with socket.create_connection((DOMAIN, 443), timeout=4):
            out['tcp443'] = 'open'
    except Exception as e:  # noqa
        out['tcp443'] = 'fail: %s' % e
    return out
