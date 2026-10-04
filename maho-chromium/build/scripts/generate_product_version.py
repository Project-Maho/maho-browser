#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys


def find_version_txt(custom_path: str | None = None) -> Path:
    if custom_path:
        p = Path(custom_path)
        if p.is_file():
            return p
    script_dir = Path(__file__).resolve().parent
    candidate = script_dir.parent.parent.parent / "maho" / "version.txt"
    if candidate.is_file():
        return candidate
    cwd_candidate = Path.cwd() / "maho" / "version.txt"
    if cwd_candidate.is_file():
        return cwd_candidate
    raise FileNotFoundError(
        f"Cannot find maho/version.txt (checked {custom_path}, {candidate}, {cwd_candidate})"
    )


def read_version(version_path: Path) -> str:
    version = version_path.read_text(encoding="utf-8").strip()
    if not version:
        raise ValueError(f"Version file {version_path} is empty")
    return version


def generate_header_content(version: str) -> str:
    return (
        "#ifndef MAHO_BROWSER_UPDATES_MAHO_PRODUCT_VERSION_H_\n"
        "#define MAHO_BROWSER_UPDATES_MAHO_PRODUCT_VERSION_H_\n"
        "\n"
        "#include <string_view>\n"
        "\n"
        "#include \"base/version.h\"\n"
        "\n"
        "namespace maho {\n"
        "namespace updates {\n"
        "\n"
        f"inline constexpr char kMahoProductVersion[] = \"{version}\";\n"
        "\n"
        "inline base::Version GetMahoProductVersion() {\n"
        "  return base::Version(std::string_view(kMahoProductVersion));\n"
        "}\n"
        "\n"
        "}  // namespace updates\n"
        "}  // namespace maho\n"
        "\n"
        "#endif  // MAHO_BROWSER_UPDATES_MAHO_PRODUCT_VERSION_H_\n"
    )


def write_file_if_changed(target_path: Path, content: str) -> bool:
    target_path.parent.mkdir(parents=True, exist_ok=True)
    if target_path.is_file():
        existing = target_path.read_text(encoding="utf-8")
        if existing == content:
            return False
    target_path.write_text(content, encoding="utf-8")
    return True


def write_depfile(depfile_path: Path, output_file: str, input_files: list[str]) -> None:
    depfile_path.parent.mkdir(parents=True, exist_ok=True)
    deps_line = " ".join(input_files)
    depfile_path.write_text(f"{output_file}: {deps_line}\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version-file")
    parser.add_argument("--output", required=True)
    parser.add_argument("--depfile")
    args = parser.parse_args()

    version_path = find_version_txt(args.version_file)
    version = read_version(version_path)
    content = generate_header_content(version)
    out_path = Path(args.output)
    write_file_if_changed(out_path, content)

    if args.depfile:
        write_depfile(Path(args.depfile), args.output, [str(version_path)])

    return 0


if __name__ == "__main__":
    sys.exit(main())
