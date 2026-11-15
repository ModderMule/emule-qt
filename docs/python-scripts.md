# Python scripts

`scripts/*.py` and `docker/*/*.py` run under one uv project at the repo root
(`pyproject.toml`, `uv.lock`, `.python-version`; no package, `.venv/` at root).

```
uv sync                                   # create/update .venv from uv.lock
uv run scripts/generate_memory_icon.py    # any script, from the repo root
uv add <pkg>                              # new dependency → pyproject + lock
```

- third-party deps: `pillow` (icon generators, splash, sprites), `cairosvg`
  (`generate_splash.py`, `generate_donkey_icons.py`; needs system cairo: `brew install cairo`)
- dev group: `cbor2` (`test-scripts/`: IPC client to emulecored)
- all other scripts are stdlib only; shell scripts and `test-scripts/*.sh` call Python through `scripts/py.sh` (`py`):
  uv project when uv is installed, else plain `python3` (CI containers)
