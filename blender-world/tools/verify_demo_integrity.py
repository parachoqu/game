"""Verify that the original demo tree still matches the recorded baseline."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORLD = ROOT / "blender-world"
DEMO = ROOT / "demo"
BEFORE = WORLD / "reports/demo-before.sha256"
AFTER = WORLD / "reports/demo-after.sha256"
REPORT = WORLD / "reports/demo-integrity.json"
STABILITY_BASELINE = WORLD / "reports/demo-stability-baseline.sha256"


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def snapshot() -> list[str]:
    records = []
    # Match the original `find -type f` snapshot: symlinks in node_modules/.bin
    # are intentionally excluded instead of being followed as regular files.
    for path in sorted(item for item in DEMO.rglob("*") if item.is_file() and not item.is_symlink()):
        records.append(f"{digest(path)}  {path.relative_to(ROOT).as_posix()}")
    return records


def aggregate(lines: list[str]) -> str:
    return hashlib.sha256(("\n".join(lines) + "\n").encode("utf-8")).hexdigest()


def main() -> None:
    before = BEFORE.read_text(encoding="utf-8").splitlines()
    after = snapshot()
    AFTER.write_text("\n".join(after) + "\n", encoding="utf-8")
    baseline_paths = {line.split("  ", 1)[1] for line in before}
    current_paths = {line.split("  ", 1)[1] for line in after}
    baseline_hashes = {line.split("  ", 1)[1]: line.split("  ", 1)[0] for line in before}
    current_hashes = {line.split("  ", 1)[1]: line.split("  ", 1)[0] for line in after}
    modified_paths = sorted(
        path for path in baseline_paths & current_paths
        if baseline_hashes[path] != current_hashes[path]
    )
    report = {
        "schema_version": "1.0.0",
        "passed": before == after,
        "baseline_files": len(before),
        "current_files": len(after),
        "baseline_aggregate_sha256": aggregate(before),
        "current_aggregate_sha256": aggregate(after),
        "added_paths": sorted(current_paths - baseline_paths),
        "removed_paths": sorted(baseline_paths - current_paths),
        "modified_paths": [
            {
                "path": path,
                "baseline_sha256": baseline_hashes[path],
                "current_sha256": current_hashes[path],
                "current_mtime": (ROOT / path).stat().st_mtime,
            }
            for path in modified_paths
        ],
        "changed_record_count": len(modified_paths),
    }
    if STABILITY_BASELINE.is_file():
        stability = STABILITY_BASELINE.read_text(encoding="utf-8").splitlines()
        report["stable_since_drift_detection"] = stability == after
        report["stability_baseline_aggregate_sha256"] = aggregate(stability)
    REPORT.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("DEMO_INTEGRITY", "PASS" if report["passed"] else "FAIL", len(after), report["current_aggregate_sha256"])
    if "stable_since_drift_detection" in report:
        print("DEMO_STABILITY", "PASS" if report["stable_since_drift_detection"] else "FAIL")
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
