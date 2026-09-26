"""Inspect the master Blender file for production structure and scale contracts."""

from __future__ import annotations

import json
import math
from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parents[1]


def finite_vector(values) -> bool:
    return all(math.isfinite(float(value)) for value in values)


def check(identifier: str, passed: bool, details) -> dict:
    return {"id": identifier, "passed": bool(passed), "details": details}


def peak_memory_kb() -> int | None:
    status = Path("/proc/self/status")
    if not status.is_file():
        return None
    for line in status.read_text(encoding="utf-8").splitlines():
        if line.startswith("VmHWM:"):
            return int(line.split()[1])
    return None


def ribbon_centers(obj: bpy.types.Object):
    vertices = obj.data.vertices
    return [
        ((vertices[index].co + vertices[index + 1].co) * 0.5)
        for index in range(0, len(vertices), 2)
    ]


def main() -> None:
    checks = []
    expected_scenes = {"ATLAS_MUNDO", "REGIAO_COMERCIAL", "TURBULENTA_01", "BIBLIOTECA_LOCAL", "VALIDACAO"}
    scenes = {scene.name for scene in bpy.data.scenes}
    checks.append(check("five_scenes", scenes == expected_scenes, sorted(scenes)))

    workflow = {"FONTES_PROCEDURAIS", "BASE_EDITAVEL", "ACABAMENTO_MANUAL", "PREPARACAO_EXPORTACAO"}
    collection_names = {collection.name for collection in bpy.data.collections}
    checks.append(check("workflow_collections", workflow.issubset(collection_names), sorted(workflow & collection_names)))

    images = []
    image_passed = True
    for image in bpy.data.images:
        if image.source != "FILE":
            continue
        packed = bool(image.packed_file)
        valid_size = image.size[0] > 0 and image.size[1] > 0
        image_passed &= packed and valid_size
        images.append({"name": image.name, "packed": packed, "size": list(image.size)})
    checks.append(check("packed_images", image_passed and len(images) >= 20, images))

    invalid_objects = []
    for obj in bpy.data.objects:
        if not finite_vector(obj.location) or not finite_vector(obj.rotation_euler) or not finite_vector(obj.scale):
            invalid_objects.append(obj.name)
        if obj.type in {"MESH", "CURVE", "FONT", "CAMERA", "LIGHT"} and obj.data is None:
            invalid_objects.append(obj.name)
    checks.append(check("valid_object_transforms", not invalid_objects, invalid_objects))

    region = bpy.data.scenes["REGIAO_COMERCIAL"]
    chunks = sorted((obj for obj in region.objects if obj.get("terrain_chunk")), key=lambda obj: obj.get("chunk_id"))
    checks.append(check("sixteen_region_chunks", len(chunks) == 16, [obj.get("chunk_id") for obj in chunks]))

    river = bpy.data.objects["REGION_MainRiver"]
    river_centers = ribbon_centers(river)
    river_z = [float(point.z) for point in river_centers]
    checks.append(check("river_surface_and_outlets", (
        abs(float(river_centers[0].x) + 512.0) < 1e-5
        and abs(float(river_centers[-1].x) - 512.0) < 1e-5
        and max(river_z) - min(river_z) <= 1e-6
    ), {
        "west_x": float(river_centers[0].x),
        "east_x": float(river_centers[-1].x),
        "surface_range_m": max(river_z) - min(river_z),
    }))

    route_details = {}
    route_passed = True
    for obj in region.objects:
        route_id = obj.get("route_id")
        if not route_id:
            continue
        centers = ribbon_centers(obj)
        slopes = []
        for first, second in zip(centers, centers[1:]):
            horizontal = math.hypot(second.x - first.x, second.y - first.y)
            slopes.append(math.degrees(math.atan2(abs(second.z - first.z), horizontal)))
        maximum = max(slopes, default=0.0)
        declared = float(obj.get("max_sustained_slope_deg", 90.0))
        route_details[route_id] = {"measured_max_slope_deg": maximum, "declared_sustained_deg": declared}
        route_passed &= maximum <= declared + 1e-4
    checks.append(check("modeled_route_slopes", route_passed and len(route_details) == 4, route_details))

    human = bpy.data.objects["VALIDATE_HumanScale_1_8m"]
    human_height = max(vertex.co.z for vertex in human.data.vertices) - min(vertex.co.z for vertex in human.data.vertices)
    bridge = bpy.data.objects["ASSET_Bridge"]
    wall = bpy.data.objects["ASSET_Wall"]
    scale_details = {
        "human_reference_height_m": human_height,
        "bridge_dimensions_m": [float(value) for value in bridge.dimensions],
        "wall_dimensions_m": [float(value) for value in wall.dimensions],
    }
    checks.append(check("scale_references", (
        abs(human_height - 1.8) < 1e-5
        and max(bridge.dimensions.x, bridge.dimensions.y) >= 4.0
        and wall.dimensions.z >= 3.0
    ), scale_details))

    cameras = [obj for obj in bpy.data.objects if obj.type == "CAMERA" and obj.get("validation_camera")]
    checks.append(check("validation_cameras", len(cameras) == 12 and all(camera.data.clip_end >= 2000.0 for camera in cameras), {
        "count": len(cameras),
        "minimum_clip_end": min((camera.data.clip_end for camera in cameras), default=0.0),
    }))

    asset_types = sorted({obj.get("asset_type") for obj in bpy.data.scenes["BIBLIOTECA_LOCAL"].objects if obj.get("asset_type")})
    checks.append(check("environment_asset_library", len(asset_types) >= 18, asset_types))

    manual_collections = [collection.name for collection in bpy.data.collections if "ACABAMENTO_MANUAL" in collection.name]
    checks.append(check("manual_layers_present", len(manual_collections) >= 6, sorted(manual_collections)))

    master_bytes = Path(bpy.data.filepath).stat().st_size
    memory_kb = peak_memory_kb()
    checks.append(check("resource_limits", master_bytes < 2_000_000_000 and (memory_kb is None or memory_kb < 12 * 1024 * 1024), {
        "blend_bytes": master_bytes,
        "peak_memory_kb_during_validation": memory_kb,
        "memory_limit_kb": 12 * 1024 * 1024,
    }))

    report = {
        "schema_version": "1.0.0",
        "blender_version": bpy.app.version_string,
        "blend_file": bpy.data.filepath,
        "passed": all(item["passed"] for item in checks),
        "counts": {
            "objects": len(bpy.data.objects),
            "materials": len(bpy.data.materials),
            "images": len(bpy.data.images),
            "node_groups": len(bpy.data.node_groups),
        },
        "checks": checks,
    }
    path = ROOT / "reports/blend-validation.json"
    path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("BLEND_VALIDATION", "PASS" if report["passed"] else "FAIL", report["counts"])
    if not report["passed"]:
        for item in checks:
            if not item["passed"]:
                print("FAILED", item["id"], item["details"])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
