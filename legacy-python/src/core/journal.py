"""Action journal: every Apply stores its pre-state (backup path or values) for Revert."""
import json
import os
import time

from . import log

LOG = log.get_logger()


def path_for(db_path: str) -> str:
    return os.path.join(os.path.dirname(db_path or os.path.expanduser('~')), 'tuner-journal.json')


def load(jpath: str) -> dict:
    try:
        with open(jpath, encoding='utf-8') as f:
            return json.load(f)
    except Exception:
        return {}


def record(jpath: str, action: str, entry: dict):
    j = load(jpath)
    j[action] = dict(entry, time=time.strftime('%Y-%m-%d %H:%M:%S'))
    with open(jpath, 'w', encoding='utf-8') as f:
        json.dump(j, f, indent=1)
    LOG.info('journal %s -> %s', action, log.scrub(str(entry)))


def pop(jpath: str, action: str) -> dict:
    j = load(jpath)
    e = j.pop(action, {})
    with open(jpath, 'w', encoding='utf-8') as f:
        json.dump(j, f, indent=1)
    return e
