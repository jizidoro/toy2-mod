"""Set keys in game\\dgVoodoo.conf by section, keeping its CRLF line endings and comments.

Usage: py dgconf.py DirectX.Antialiasing=8x DirectX.Filtering=16 [...]
(Antialiasing, Filtering, ... exist in several sections, so the section is required.)
"""
import pathlib
import re
import sys

CONF = pathlib.Path(__file__).resolve().parent.parent / "game" / "dgVoodoo.conf"


def main():
    want = {}
    for arg in sys.argv[1:]:
        key, value = arg.split("=", 1)
        section, name = key.split(".", 1)
        want[(section, name)] = value
    lines = CONF.read_bytes().decode("latin-1").split("\r\n")
    section, done = None, set()
    for i, line in enumerate(lines):
        m = re.match(r"\[(.+)\]", line.strip())
        if m:
            section = m.group(1)
            continue
        km = re.match(r"^(\s*)([A-Za-z0-9_]+)(\s*=\s*)(.*)$", line)
        if km and (section, km.group(2)) in want:
            lines[i] = km.group(1) + km.group(2) + km.group(3) + want[(section, km.group(2))]
            done.add((section, km.group(2)))
    missing = set(want) - done
    if missing:
        sys.exit(f"not found in {CONF}: {sorted(missing)}")
    CONF.write_bytes("\r\n".join(lines).encode("latin-1"))
    for (s, k), v in sorted(want.items()):
        print(f"[{s}] {k} = {v}")


if __name__ == "__main__":
    main()
