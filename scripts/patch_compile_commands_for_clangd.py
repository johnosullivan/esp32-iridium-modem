#!/usr/bin/env python3
"""Patch ESP-IDF compile_commands.json so clangd can find newlib headers.

xtensa-esp*-elf-gcc keeps sys/reent.h in its sysroot. clangd does not inherit
that sysroot unless --query-driver is used (which often breaks on unsupported
--target=xtensa). This script injects -isystem paths from the compiler already
recorded in compile_commands.json.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path


def compiler_include_flags(compiler: str) -> list[str]:
    gcc = Path(compiler)
    if not gcc.exists():
        raise FileNotFoundError(compiler)

    sysroot = subprocess.check_output([str(gcc), "-print-sysroot"], text=True).strip()
    include = subprocess.check_output([str(gcc), "-print-file-name=include"], text=True).strip()
    include_fixed = subprocess.check_output(
        [str(gcc), "-print-file-name=include-fixed"], text=True
    ).strip()

    flags = [
        f"-isystem{sysroot}/include",
        f"-isystem{include}",
        "-D__XTENSA__",
        "-D__xtensa__",
        "-fno-blocks",
    ]
    if Path(include_fixed).exists():
        flags.append(f"-isystem{include_fixed}")
    return flags


def patch_command(command: str, extras: list[str]) -> str:
    marker = extras[0]
    if marker in command:
        return command
    parts = command.split(" ", 1)
    if len(parts) != 2:
        return command
    return parts[0] + " " + " ".join(extras) + " " + parts[1]


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <compile_commands.json>", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    if not path.is_file():
        print(f"missing {path}", file=sys.stderr)
        return 1

    data = json.loads(path.read_text())
    if not data:
        return 0

    first = data[0].get("command") or ""
    compiler = first.split(" ", 1)[0]
    extras = compiler_include_flags(compiler)

    changed = 0
    for entry in data:
        cmd = entry.get("command")
        if not cmd:
            continue
        new_cmd = patch_command(cmd, extras)
        if new_cmd != cmd:
            entry["command"] = new_cmd
            changed += 1

    path.write_text(json.dumps(data, indent=2) + "\n")
    print(f"patched {changed} compile commands for clangd ({Path(compiler).name})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
