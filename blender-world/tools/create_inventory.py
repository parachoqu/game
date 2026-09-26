"""Create a deterministic file inventory for the complete Blender delivery."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "reports/file-inventory.json"


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(4 * 1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def category(relative: Path) -> str:
    if relative.suffix == ".blend" or relative.suffix == ".blend1":
        return "blender_master"
    if relative.parts[0] == "exports":
        return "export"
    if relative.parts[0] == "renders":
        return "render"
    if relative.parts[0] == "reports":
        return "report"
    if relative.parts[0] == "tools":
        return "tool"
    if relative.parts[0] == "tests":
        return "test"
    return "source_or_documentation"


def main() -> None:
    paths = sorted(
        path for path in ROOT.rglob("*")
        if path.is_file() and path != OUTPUT and "__pycache__" not in path.parts
    )
    records = []
    totals: dict[str, int] = {}
    for path in paths:
        relative = path.relative_to(ROOT)
        item_category = category(relative)
        size = path.stat().st_size
        totals[item_category] = totals.get(item_category, 0) + size
        records.append({
            "path": relative.as_posix(),
            "category": item_category,
            "bytes": size,
            "sha256": digest(path),
        })
    report = {
        "schema_version": "1.0.0",
        "file_count": len(records),
        "total_bytes_excluding_inventory": sum(item["bytes"] for item in records),
        "bytes_by_category": dict(sorted(totals.items())),
        "files": records,
    }
    OUTPUT.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("FILE_INVENTORY_OK", report["file_count"], report["total_bytes_excluding_inventory"])


if __name__ == "__main__":
    main()
