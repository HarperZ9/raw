"""A test plugin for one harness run: deployed before the game starts, gone after it, checked by hash.
Original work, MIT.

The folder holds one .esp or .esl and an optional Scripts/ folder of .pex files (tools/papyrus/make_load_plugin.py).
Before: Plugins.txt (the game's load order in %LOCALAPPDATA%/Skyrim Special Edition) is copied to the run's output
with its hash; the plugin goes into Data and its scripts into Data/Scripts, never over an existing file; the
plugin's line is appended (light plugins with '*', active). After: every added file is removed and Plugins.txt is
written back from the copy and its hash checked.
"""
from __future__ import annotations

import hashlib
import os
import shutil
from pathlib import Path


def _sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else ""


class TestPlugin:
    def __init__(self, game: Path, folder: Path, out: Path):
        self.game, self.folder, self.out = game, folder, out
        self.list = Path(os.environ["LOCALAPPDATA"]) / "Skyrim Special Edition" / "Plugins.txt"
        self.added: list[Path] = []
        self.backup = out / "Plugins.txt.before"
        self.sha_before = ""

    def deploy(self) -> dict:
        plugins = [p for p in self.folder.iterdir() if p.suffix.lower() in (".esp", ".esl")]
        if len(plugins) != 1:
            raise SystemExit(f"{self.folder}: expected one plugin, found {len(plugins)}")
        srcs = [(plugins[0], self.game / "Data" / plugins[0].name)]
        scripts = self.folder / "Scripts"
        if scripts.is_dir():
            srcs += [(p, self.game / "Data" / "Scripts" / p.name) for p in scripts.glob("*.pex")]
        for _, dst in srcs:
            if dst.exists():
                raise SystemExit(f"{dst} already exists; refusing to overwrite it")
        self.out.mkdir(parents=True, exist_ok=True)
        self.sha_before = _sha(self.list)
        if self.list.exists():
            shutil.copy2(self.list, self.backup)
        for src, dst in srcs:
            shutil.copy2(src, dst)
            self.added.append(dst)
        text = self.list.read_text(encoding="utf-8") if self.list.exists() else ""
        if text and not text.endswith("\n"):
            text += "\n"
        self.list.write_text(text + f"*{plugins[0].name}\n", encoding="utf-8")
        return {"plugin": plugins[0].name, "files": [str(p) for p in self.added], "plugins_txt_sha_before": self.sha_before}

    def restore(self) -> dict:
        for p in self.added:
            p.unlink(missing_ok=True)
        if self.backup.exists():
            shutil.copy2(self.backup, self.list)
        elif self.list.exists() and not self.sha_before:
            self.list.unlink()
        after = _sha(self.list)
        return {"removed": all(not p.exists() for p in self.added), "plugins_txt_restored": after == self.sha_before,
                "plugins_txt_sha_after": after}
