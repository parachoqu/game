"""Append a procedural source version without touching editable/manual layers."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import bpy
import numpy as np


TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from worldgen_core import generate_world_data, load_config


def parse_args():
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--kinds", nargs="+", choices=("atlas", "region", "turbulent"), default=("atlas", "region", "turbulent"))
    parser.add_argument("--save", action="store_true", help="Save the appended versions into the open master file")
    return parser.parse_args(values)


def collection_signature(token: str) -> str:
    records = []
    for collection in sorted((item for item in bpy.data.collections if token in item.name), key=lambda item: item.name):
        records.append({
            "collection": collection.name,
            "objects": [
                {
                    "name": obj.name,
                    "type": obj.type,
                    "location": [round(float(value), 8) for value in obj.location],
                    "rotation": [round(float(value), 8) for value in obj.rotation_euler],
                    "scale": [round(float(value), 8) for value in obj.scale],
                    "data": obj.data.name if obj.data else None,
                }
                for obj in sorted(collection.objects, key=lambda item: item.name)
            ],
        })
    payload = json.dumps(records, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def data_hash(data) -> str:
    digest = hashlib.sha256()
    for field in ("height", "land", "water", "moisture", "slope", "biome"):
        digest.update(np.ascontiguousarray(getattr(data, field)).tobytes())
    return digest.hexdigest()


def next_version(kind: str, seed: int) -> int:
    expression = re.compile(rf"^GEN_{kind.upper()}__seed_{seed}__v(\d+)$")
    versions = []
    for collection in bpy.data.collections:
        match = expression.match(collection.name)
        if match:
            versions.append(int(match.group(1)))
    return max(versions, default=0) + 1


def append_version(kind: str, config: dict) -> dict:
    data = generate_world_data(config, kind)
    seed = config["seeds"][kind]
    version = next_version(kind, seed)
    name = f"GEN_{kind.upper()}__seed_{seed}__v{version:03d}"
    collection = bpy.data.collections.new(name)
    collection["generation_kind"] = kind
    collection["seed"] = seed
    collection["version"] = version
    collection["data_hash"] = data_hash(data)
    collection["height_shape"] = list(data.height.shape)
    collection["source_data"] = f"//source/data/{kind}.npz"
    collection["regeneration_policy"] = "append-only source version"
    bpy.data.collections["FONTES_PROCEDURAIS"].children.link(collection)
    marker = bpy.data.objects.new(f"SOURCE_{kind.upper()}_v{version:03d}", None)
    marker["seed"] = seed
    marker["data_hash"] = collection["data_hash"]
    marker["generator"] = "worldgen_core.py"
    collection.objects.link(marker)
    return {
        "kind": kind,
        "seed": seed,
        "version": version,
        "collection": name,
        "data_hash": collection["data_hash"],
        "height_shape": list(data.height.shape),
    }


def main() -> None:
    if Path(bpy.data.filepath).name != "MUNDO_MESTRE.blend":
        raise RuntimeError("regenerate_sources.py must run with MUNDO_MESTRE.blend open")
    args = parse_args()
    config = load_config(ROOT / "config/world_config.json")
    before = {
        "manual": collection_signature("ACABAMENTO_MANUAL"),
        "editable": collection_signature("BASE_EDITAVEL"),
    }
    versions = [append_version(kind, config) for kind in args.kinds]
    after = {
        "manual": collection_signature("ACABAMENTO_MANUAL"),
        "editable": collection_signature("BASE_EDITAVEL"),
    }
    passed = before == after
    if args.save and passed:
        bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath, check_existing=False)
    report = {
        "schema_version": "1.0.0",
        "passed": passed,
        "saved": bool(args.save and passed),
        "signatures_before": before,
        "signatures_after": after,
        "versions_created": versions,
        "policy": "append procedural version; never modify BASE_EDITAVEL or ACABAMENTO_MANUAL",
    }
    path = ROOT / "reports/regeneration-safety.json"
    path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("REGENERATION_SAFETY", "PASS" if passed else "FAIL", [item["collection"] for item in versions])
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
