# Contributing to GameLoop Tuner

Thank you for your interest in contributing! This document outlines the process and standards for contributions.

## 🎯 Ways to Contribute

- **Bug reports** — Found a regression or edge case? Open an issue with steps to reproduce.
- **Feature requests** — Have an idea for a new fix or improvement? Discuss in an issue first.
- **Code contributions** — Fixes, refactors, new modules via pull requests.
- **Documentation** — Improve README, add comments, write guides.
- **Testing** — Test on different GameLoop versions, Windows versions, hardware.

## 🛠️ Development Setup

```bash
# Fork and clone
git clone https://github.com/YOUR_FORK/gameloop-tuner.git
cd gameloop-tuner

# Create virtual environment
python -m venv .venv
.venv\Scripts\activate  # Windows

# Install in development mode with dev dependencies
pip install -e .[dev]

# Verify
ruff check src/
pytest
python -m src.main  # Requires Admin + GameLoop installed
```

## 📝 Code Standards

### Python Style
- **Formatter**: Ruff (`ruff format src/`)
- **Linter**: Ruff (`ruff check src/`)
- **Type hints**: Encouraged (mypy config in `pyproject.toml`)
- **Line length**: 100 chars (Ruff default)

### Architecture Rules
1. **No hardcoded paths** — Use `core.paths` module for all path discovery
2. **Portable logic in `core/`** — No UI imports in core modules
3. **DB writes via `guarded_updates()`** — Automatic backup + verify on re-read
4. **Fix functions return `list[str]` notes** — For logging/UI display
5. **Dry-run support** — Every fix accepts `dry=False` parameter

### Commit Messages
Follow conventional commits:
```
feat(keymaps): add blind for Tex 30123 (new DMZ trigger)
fix(perf): clarity tier 1 CPU fallback detection
docs(readme): clarify admin requirement for quarantine
refactor(core): extract path discovery to paths.py
```

## 🧪 Testing

```bash
# Run all tests
pytest

# Run with coverage
pytest --cov=src --cov-report=html

# Run specific module
pytest tests/test_keymaps.py -v
```

**Test Requirements**:
- Tests should not require GameLoop installed (mock DB/files)
- Integration tests marked with `@pytest.mark.integration` (run manually)
- Test data in `tests/fixtures/`

## 🔧 Adding a New Fix

1. **Add fix function** in `src/ui/pages.py`:
```python
def fix_my_new_thing(dry=False):
    dbp = _need_db()
    # ... logic ...
    return dbmod.guarded_updates(dbp, ops, dry_run=dry)['notes']
```

2. **Register in `FIXES` dict** (same file):
```python
'myfix': dict(
    tab='Keymaps',  # or 'Performance', 'VM & Priority'
    label='Human readable label',
    why='Why this fix exists, what it does',
    apply=lambda dry: (fix_my_new_thing(dry), None)
),
```

3. **Add state detection** in `detect_applied()` if needed for UI pre-check

4. **Add drift monitoring** in `drift.py` if the fix can be reverted by GameLoop

5. **Write tests** in `tests/`

## 📦 Building Releases

Requires Visual Studio 2022 or Build Tools with the *Desktop development with
C++* workload, plus the Windows SDK and CMake ≥ 3.24. Outside a developer
command prompt, run the build through `vcvars64.bat` or the STL and SDK headers
will not be on the include path.

```powershell
cmake -S native -B native/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --config Release
ctest --test-dir native/build --output-on-failure
```

Two binaries land in `native/build/bin/`:

| Binary | What it is |
|---|---|
| `GameLoopTuner.exe` | the product |
| `GameLoopTunerSetup.exe` | the installer |

Both use the static CRT and import only system DLLs, so neither requires a
runtime to be installed. The installer is a separate target that deliberately
does **not** link `glt_core` — keep it that way, or it grows from ~180 KB to
~1.7 MB. Turn it off with `-DGLT_BUILD_INSTALLER=OFF`.

Tag the commit (`git tag v2.0.0 && git push --tags`); the `Release` workflow
builds, tests and publishes both binaries.

## 🐛 Bug Report Template

When filing a bug, include:

1. **GameLoop version** (from About dialog or `Application\<ver>\`)
2. **Windows version** (`winver`)
3. **Python version** (if dev) / **Tuner version** (if release)
4. **Steps to reproduce**
5. **Expected vs actual behavior**
6. **Logs** (from Logs tab or `%TEMP%\gameloop-tuner.log`)
7. **Dashboard screenshot** (if relevant)

## 🔒 Security

- No network calls except optional hosts file modification
- No data collection or telemetry
- All writes are local, backed up, and reversible
- Report security issues privately via GitHub Security Advisories

## 📋 Pull Request Checklist

- [ ] Ruff passes: `ruff check src/ && ruff format --check src/`
- [ ] Tests pass: `pytest`
- [ ] Type hints added for new functions
- [ ] Docstrings for public functions
- [ ] CHANGELOG.md updated (under `## Unreleased`)
- [ ] No hardcoded paths added
- [ ] Dry-run support for new fixes
- [ ] Backup/verify for DB writes

## 💬 Community

- Be respectful and constructive
- Help others in issues/discussions
- Share your GameLoop version findings

## 📜 License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0 (same as the project).