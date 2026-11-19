"""SKSE plugins for one harness run, hosted by RAW's raw_skse.dll: deployed before the game starts, gone after it.
Original work, MIT.

Before: every file given goes into Data/SKSE/Plugins, never over an existing one. The game's SKSE log folder
(Documents/My Games/Skyrim Special Edition/SKSE) and save folder are listed with hashes.
After: the files leave Data/SKSE/Plugins; the logs the run wrote come to <out>/skse_logs; a log the run created is
removed and one it changed is written back from its copy; saves and co-saves the run created whose names start with
a test prefix (rawskse) come to <out>/saves and are removed. Anything else that changed is listed, never touched.
"""
from __future__ import annotations

import hashlib
import shutil
from pathlib import Path

DOCS = Path.home() / "Documents" / "My Games" / "Skyrim Special Edition"
SAVE_PREFIX = "rawskse"


def _sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def _listing(d: Path) -> dict[str, str]:
    return {p.name: _sha(p) for p in d.iterdir() if p.is_file()} if d.is_dir() else {}


def save_dir() -> Path:
    sub = "Saves\\"
    for ini in ("SkyrimCustom.ini", "Skyrim.ini"):
        p = DOCS / ini
        if p.exists():
            for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
                k, _, v = line.partition("=")
                if k.strip().lower() == "slocalsavepath" and v.strip():
                    sub = v.strip()
                    break
            else:
                continue
            break
    return DOCS / sub.rstrip("\\/")


class SksePlugins:
    def __init__(self, game: Path, files: list[Path], out: Path):
        self.dst = game / "Data" / "SKSE" / "Plugins"
        self.files, self.out = files, out
        self.logs = DOCS / "SKSE"
        self.saves = save_dir()
        self.added: list[Path] = []

    def deploy(self) -> dict:
        for f in self.files:
            if (self.dst / f.name).exists():
                raise SystemExit(f"{self.dst / f.name} already exists; refusing to overwrite it")
        self.out.mkdir(parents=True, exist_ok=True)
        self.log_before = _listing(self.logs)
        self.log_copy = self.out / "skse_logs_before"
        if self.logs.is_dir():
            shutil.copytree(self.logs, self.log_copy, dirs_exist_ok=True)
        self.saves_before = _listing(self.saves)
        self.dst.mkdir(parents=True, exist_ok=True)
        for f in self.files:
            shutil.copy2(f, self.dst / f.name)
            self.added.append(self.dst / f.name)
        return {"skse_files": [p.name for p in self.added], "save_dir": str(self.saves)}

    def restore(self) -> dict:
        for p in self.added:
            p.unlink(missing_ok=True)
        logs_out = self.out / "skse_logs"
        logs_out.mkdir(parents=True, exist_ok=True)
        created, restored = [], []
        for name, h in _listing(self.logs).items():
            if self.log_before.get(name) == h:
                continue
            shutil.copy2(self.logs / name, logs_out / name)
            if name in self.log_before:
                shutil.copy2(self.log_copy / name, self.logs / name)
                restored.append(name)
            else:
                (self.logs / name).unlink()
                created.append(name)
        logs_ok = _listing(self.logs) == self.log_before
        shutil.rmtree(self.log_copy, ignore_errors=True)
        saves_out = self.out / "saves"
        removed, other = [], []
        for name, h in _listing(self.saves).items():
            if self.saves_before.get(name) == h:
                continue
            if name.lower().startswith(SAVE_PREFIX) and name not in self.saves_before:
                saves_out.mkdir(parents=True, exist_ok=True)
                shutil.move(str(self.saves / name), saves_out / name)
                removed.append(name)
            else:
                other.append(name)
        return {"skse_files_removed": all(not p.exists() for p in self.added), "logs_created_removed": created,
                "logs_restored": restored, "logs_folder_as_before": logs_ok, "test_saves_removed": removed,
                "other_save_changes": other}
