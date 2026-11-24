"""Check that relative markdown links in InferX resolve to existing files.

Covers README.md and every markdown file under docs/, experiments/ and
bench/. Links are resolved relative to the linking file. http(s), mailto and
pure #anchors are skipped.

Derived from langX bench/check_md_links.py.

Usage: python bench/check_md_links.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINK = re.compile(r"\]\(([^)\s]+)\)")
SRC = re.compile(r"""src=["']([^"']+)["']""")


def markdown_files():
    """Yield every markdown file the check covers."""
    yield ROOT / "README.md"
    for folder in ("docs", "experiments", "bench"):
        if (ROOT / folder).is_dir():
            yield from sorted((ROOT / folder).rglob("*.md"))


def main():
    """Print each broken link and return 1 if there is any, else 0."""
    broken = []
    for md in markdown_files():
        text = md.read_text(encoding="utf-8")
        for target in LINK.findall(text) + SRC.findall(text):
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            path = (md.parent / target.split("#", 1)[0]).resolve()
            if not path.exists():
                broken.append(f"{md.relative_to(ROOT)}: {target}")
    for line in broken:
        print(f"broken link: {line}")
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
