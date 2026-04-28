#!/usr/bin/env python3
"""Generate a VS Code compile_commands.json for Raspberry Pi kernel modules."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


PATH_FLAGS_WITH_VALUE = {
    "-I",
    "-include",
    "-isystem",
    "-iquote",
    "-idirafter",
}

PATH_FLAGS_JOINED = (
    "-I",
    "-isystem",
    "-iquote",
    "-idirafter",
)

def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def kernel_build_dir() -> Path:
    release = subprocess.check_output(["uname", "-r"], text=True).strip()
    return Path(f"/lib/modules/{release}/build").resolve()


def kernel_common_dir(build_dir: Path) -> Path:
    makefile = build_dir / "Makefile"
    if not makefile.exists():
        return build_dir

    for line in makefile.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line.startswith("include "):
            continue

        included = Path(line.split(None, 1)[1])
        if included.name == "Makefile" and included.parent.exists():
            return included.parent.resolve()

    return build_dir


def update_header_links(link_dir: Path, build_dir: Path, common_dir: Path) -> None:
    link_dir.mkdir(parents=True, exist_ok=True)

    for name, target in {"build": build_dir, "common": common_dir}.items():
        link = link_dir / name
        if link.is_symlink() or not link.exists():
            if link.exists() or link.is_symlink():
                link.unlink()
            link.symlink_to(target, target_is_directory=True)
            continue

        print(f"Skipped {link}: path exists and is not a symlink", file=sys.stderr)


def module_dirs(driver_dir: Path) -> list[Path]:
    return sorted(path.parent for path in driver_dir.glob("*/Makefile"))


def run_make(build_dir: Path, module_dir: Path) -> None:
    subprocess.run(
        ["make", "-C", str(build_dir), f"M={module_dir}", "modules"],
        check=True,
    )


def split_saved_command(cmd_file: Path) -> tuple[Path, list[str]] | None:
    text = cmd_file.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^savedcmd_(?P<output>.+?\.o)\s*:=\s*(?P<cmd>.+?)\s*$", text, re.M)
    if not match:
        return None

    output = Path(match.group("output")).resolve()
    if output.name.endswith(".mod.o") or output.name == ".module-common.o":
        return None

    args = shlex.split(match.group("cmd"), posix=True)
    if not args:
        return None

    source = Path(args[-1]).resolve()
    if source.suffix != ".c" or source.name.endswith(".mod.c"):
        return None

    return output, args


def absolutize_args(args: list[str], build_dir: Path) -> list[str]:
    fixed: list[str] = []
    index = 0

    compiler = shutil.which(args[0]) or args[0]
    fixed.append(compiler)
    index = 1

    while index < len(args):
        arg = args[index]

        if arg in PATH_FLAGS_WITH_VALUE and index + 1 < len(args):
            fixed.append(arg)
            fixed.append(absolutize_path_arg(args[index + 1], build_dir))
            index += 2
            continue

        joined = next((flag for flag in PATH_FLAGS_JOINED if arg.startswith(flag) and arg != flag), None)
        if joined:
            value = arg[len(joined) :]
            fixed.append(joined + absolutize_path_arg(value, build_dir))
            index += 1
            continue

        fixed.append(arg)
        index += 1

    return fixed


def absolutize_path_arg(value: str, build_dir: Path) -> str:
    path = Path(value)
    if path.is_absolute():
        return value
    return str((build_dir / path).resolve())


def source_from_args(args: list[str]) -> Path:
    return Path(args[-1]).resolve()


def output_from_args(args: list[str]) -> Path | None:
    if "-o" not in args:
        return None
    index = args.index("-o")
    if index + 1 >= len(args):
        return None
    return Path(args[index + 1]).resolve()


def retarget_args(args: list[str], source: Path, output: Path) -> list[str]:
    stem = source.stem
    retargeted = list(args)

    if "-o" in retargeted:
        output_index = retargeted.index("-o") + 1
        if output_index < len(retargeted):
            retargeted[output_index] = str(output)

    if retargeted:
        retargeted[-1] = str(source)

    for index, arg in enumerate(retargeted):
        if arg.startswith("-Wp,-MMD,"):
            retargeted[index] = f"-Wp,-MMD,{output.parent / ('.' + output.name + '.d')}"
        elif arg.startswith("-DKBUILD_BASENAME="):
            retargeted[index] = f'-DKBUILD_BASENAME="{stem}"'
        elif arg.startswith("-DKBUILD_MODNAME="):
            retargeted[index] = f'-DKBUILD_MODNAME="{stem}"'
        elif arg.startswith("-D__KBUILD_MODNAME="):
            retargeted[index] = f"-D__KBUILD_MODNAME=kmod_{stem}"

    return retargeted


def compile_entries(build_dir: Path, modules: list[Path]) -> list[dict[str, object]]:
    entries: dict[Path, dict[str, object]] = {}
    templates: dict[Path, list[str]] = {}

    for module_dir in modules:
        for cmd_file in sorted(module_dir.glob(".*.o.cmd")):
            parsed = split_saved_command(cmd_file)
            if parsed is None:
                continue

            output, raw_args = parsed
            args = absolutize_args(raw_args, build_dir)
            source = source_from_args(args)
            output = output_from_args(args) or output

            if not source.is_relative_to(module_dir):
                continue

            templates.setdefault(module_dir, args)
            entries[source] = {
                "directory": str(build_dir),
                "file": str(source),
                "arguments": args,
                "output": str(output),
            }

    for module_dir in modules:
        template = templates.get(module_dir)
        if template is None:
            continue

        for source in sorted(module_dir.glob("*.c")):
            if source.name.endswith(".mod.c") or source in entries:
                continue

            output = source.with_suffix(".o")
            args = retarget_args(template, source.resolve(), output.resolve())
            entries[source.resolve()] = {
                "directory": str(build_dir),
                "file": str(source.resolve()),
                "arguments": args,
                "output": str(output.resolve()),
            }

    return [entries[path] for path in sorted(entries)]


def write_compile_commands(output: Path, entries: list[dict[str, object]]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--driver-dir", type=Path, default=root / "rpi_driver")
    parser.add_argument("--kernel-build", type=Path, default=kernel_build_dir())
    parser.add_argument("--output", type=Path, default=root / ".vscode" / "compile_commands.json")
    parser.add_argument("--link-dir", type=Path, default=root / ".vscode" / "kernel-headers")
    parser.add_argument("--no-build", action="store_true", help="only parse existing Kbuild .cmd files")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_dir = args.kernel_build.resolve()
    common_dir = kernel_common_dir(build_dir)
    modules = module_dirs(args.driver_dir.resolve())

    if not modules:
        print(f"No kernel module Makefiles found under {args.driver_dir}", file=sys.stderr)
        return 1

    if not args.no_build:
        for module_dir in modules:
            run_make(build_dir, module_dir)

    entries = compile_entries(build_dir, modules)
    if not entries:
        print("No Kbuild .cmd compile commands found. Build the modules first.", file=sys.stderr)
        return 1

    update_header_links(args.link_dir.resolve(), build_dir, common_dir)
    write_compile_commands(args.output.resolve(), entries)
    print(f"Wrote {len(entries)} entries to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
