#!/usr/bin/env python3
"""
Convert NHDDL YAML-style config files to CNF format.

Old format: key: value, $key: value (disabled), # comments
New format: -key=value, #-key=value (disabled), # comments

Reads from files or stdin, writes to stdout or .cnf files.
"""

import re
import sys


def convert_line(line: str) -> str | None:
    """Convert a single YAML-style line to CNF. Returns None to skip (e.g. empty)."""
    line = line.rstrip("\r\n")
    stripped = line.strip()
    if not stripped:
        return ""
    # Already CNF format (starts with - or #- / # -): pass through
    if stripped[0] == "-":
        return line
    if stripped[0] == "#":
        rest = stripped[1:].lstrip()
        if rest.startswith("-"):
            return line  # already #-key=value
        # Old-style commented line: # key: value or #key: value → disabled
        idx = rest.find(":")
        if idx >= 0:
            key = rest[:idx].strip()
            value = rest[idx + 1 :].strip()
            value = re.sub(r"\s*#.*$", "", value).strip()
            if key:
                out = f"#-{key}={value}" if value else f"#-{key}"
                return out
        return line  # plain comment
    # key: value or $key: value
    idx = stripped.find(":")
    if idx < 0:
        # Not a valid YAML-style line; output as comment so it's not lost
        return "# " + stripped
    key = stripped[:idx].strip()
    value = stripped[idx + 1 :].strip()
    # Strip trailing # and inline comment
    value = re.sub(r"\s*#.*$", "", value).strip()
    disabled = key.startswith("$")
    if disabled:
        key = key[1:].strip()
    if not key:
        return ""
    if value:
        out = f"-{key}={value}" if not disabled else f"#-{key}={value}"
    else:
        out = f"-{key}" if not disabled else f"#-{key}"
    return out


def convert_stream(instream, outstream):
    """Convert lines from instream to outstream."""
    for line in instream:
        result = convert_line(line)
        if result is not None:
            outstream.write(result + "\n")


def convert_file(path: str, out_path: str | None = None) -> None:
    """Convert a single file. If out_path is None, write to path with .yaml replaced by .cnf."""
    if out_path is None:
        out_path = path.replace(".yaml", ".cnf") if path.endswith(".yaml") else path + ".cnf"
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    with open(out_path, "w", encoding="utf-8") as out:
        for line in lines:
            result = convert_line(line)
            if result is not None:
                out.write(result + "\n")
    print(f"{path} -> {out_path}", file=sys.stderr)


def main():
    if len(sys.argv) < 2:
        convert_stream(sys.stdin, sys.stdout)
        return
    for path in sys.argv[1:]:
        convert_file(path)


if __name__ == "__main__":
    main()
