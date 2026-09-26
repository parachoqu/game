"""Reopen every exported GLB in Blender and inspect meshes, materials and bounds."""

from __future__ import annotations

import json
import math
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]
EXPORTS = ROOT / "exports"


def peak_memory_kb() -> int | None:
    path = Path("/proc/self/status")
    if not path.is_file():
        return None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("VmHWM:"):
            return int(line.split()[1])
    return None


def inspect_glb(path: Path) -> dict:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    result = bpy.ops.import_scene.gltf(filepath=str(path))
    if "FINISHED" not in result:
        raise RuntimeError(f"could not import {path}")
    objects = list(bpy.context.scene.objects)
    meshes = [obj for obj in objects if obj.type == "MESH"]
    vertices = sum(len(obj.data.vertices) for obj in meshes)
    points = []
    for obj in meshes:
        obj.data.calc_loop_triangles()
        points.extend(obj.matrix_world @ Vector(corner) for corner in obj.bound_box)
    triangles = sum(len(obj.data.loop_triangles) for obj in meshes)
    if not meshes or vertices == 0 or not points:
        raise RuntimeError(f"GLB contains no usable mesh: {path}")
    bounds = {
        "min": [min(float(point[index]) for point in points) for index in range(3)],
        "max": [max(float(point[index]) for point in points) for index in range(3)],
    }
    if not all(math.isfinite(value) for side in bounds.values() for value in side):
        raise RuntimeError(f"non-finite bounds: {path}")
    return {
        "file": str(path.relative_to(EXPORTS)),
        "bytes": path.stat().st_size,
        "objects": len(objects),
        "meshes": len(meshes),
        "vertices": vertices,
        "triangles": triangles,
        "materials": sorted(material.name for material in bpy.data.materials),
        "bounds": bounds,
        "origins": {
            obj.name: [round(float(value), 6) for value in obj.location]
            for obj in meshes
        },
    }


def main() -> None:
    manifest = json.loads((EXPORTS / "manifest.json").read_text(encoding="utf-8"))
    paths = [EXPORTS / relative for relative in manifest["export_files"] if relative.endswith(".glb")]
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise RuntimeError(f"missing GLBs: {missing}")
    records = []
    failures = []
    for path in paths:
        try:
            record = inspect_glb(path)
            records.append(record)
            print("GLB_OK", record["file"], record["meshes"], record["vertices"])
        except Exception as error:
            failures.append({"file": str(path.relative_to(EXPORTS)), "error": str(error)})
            print("GLB_FAIL", path, error)
    memory_kb = peak_memory_kb()
    report = {
        "schema_version": "1.0.0",
        "blender_version": bpy.app.version_string,
        "passed": len(records) == len(paths) and not failures and (memory_kb is None or memory_kb < 12 * 1024 * 1024),
        "expected_glbs": len(paths),
        "validated_glbs": len(records),
        "peak_memory_kb": memory_kb,
        "memory_limit_kb": 12 * 1024 * 1024,
        "records": records,
        "failures": failures,
    }
    output = ROOT / "reports/glb-validation.json"
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("GLB_VALIDATION", "PASS" if report["passed"] else "FAIL", len(records), memory_kb)
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
