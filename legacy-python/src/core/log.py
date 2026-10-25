"""Rotating file log + in-memory ring for the GUI. Secrets never logged (see scrub)."""
import logging
import os
import tempfile
from logging.handlers import RotatingFileHandler

SECRET_KEYS = ('seed_id', 'vdid', 'oaid', 'ams_union_id', 'cert_hash', 'VPoDioa', 'VOPoDioa', 'VWiDioa')

_mem: list = []


class _MemHandler(logging.Handler):
    def emit(self, record):
        _mem.append(self.format(record))
        del _mem[:-500]


def scrub(text: str) -> str:
    import re
    out = str(text)
    pat = re.compile(r'("?)(%s)\1\s*[:=]\s*"?[^",\s}\]]+"?' % '|'.join(SECRET_KEYS))
    return pat.sub(r'"\2": "***"', out)


_logger = None


def get_logger(name: str = 'tuner') -> logging.Logger:
    global _logger
    if _logger:
        return _logger
    _logger = logging.getLogger(name)
    _logger.setLevel(logging.INFO)
    fmt = logging.Formatter('%(asctime)s %(levelname)s %(message)s')
    try:
        logdir = os.path.join(tempfile.gettempdir(), 'gameloop-tuner')
        os.makedirs(logdir, exist_ok=True)
        fh = RotatingFileHandler(os.path.join(logdir, 'tuner.log'), maxBytes=1_000_000, backupCount=3,
                                 encoding='utf-8')
        fh.setFormatter(fmt)
        _logger.addHandler(fh)
    except Exception:
        pass
    mh = _MemHandler()
    mh.setFormatter(fmt)
    _logger.addHandler(mh)
    return _logger


def mem_lines() -> list:
    return list(_mem)
