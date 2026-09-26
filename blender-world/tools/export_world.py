"""Export production GLBs and machine-readable contracts from MUNDO_MESTRE."""

from __future__ import annotations

import json
import math
from pathlib import Path
import sys

import bpy


TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
EXPORTS = ROOT / "exports"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from worldgen_core import build_route_polyline, generate_world_data, load_config


def json_write(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def renderable(objects):
    return [obj for obj in objects if obj.type in {"MESH", "CURVE", "FONT"}]


def collection_renderables(name: str):
    collection = bpy.data.collections[name]
    return renderable(collection.all_objects)


def select_only(scene: bpy.types.Scene, objects) -> list[bpy.types.Object]:
    # Selection is stored per view layer. Clear the scene used by the previous
    # export before switching, otherwise Blender's glTF operator can include
    # still-selected objects from that scene.
    for current in bpy.context.view_layer.objects:
        current.select_set(False)
    bpy.context.window.scene = scene
    bpy.context.view_layer.update()
    for current in bpy.context.view_layer.objects:
        current.select_set(False)
    selected = []
    view_names = {obj.name for obj in bpy.context.view_layer.objects}
    for obj in objects:
        if obj.name in view_names:
            obj.hide_set(False)
            obj.hide_render = False
            obj.select_set(True)
            selected.append(obj)
    if not selected:
        raise RuntimeError(f"no exportable objects selected in {scene.name}")
    bpy.context.view_layer.objects.active = selected[0]
    return selected


def export_glb(path: Path, scene: bpy.types.Scene, objects) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    selected = select_only(scene, objects)
    result = bpy.ops.export_scene.gltf(
        filepath=str(path),
        export_format="GLB",
        use_selection=True,
        export_apply=True,
        export_attributes=True,
        export_extras=True,
        export_cameras=False,
        export_lights=False,
        export_yup=True,
        export_materials="EXPORT",
        export_image_format="AUTO",
        export_image_add_webp=False,
        export_draco_mesh_compression_enable=False,
    )
    if "FINISHED" not in result or not path.is_file():
        raise RuntimeError(f"GLB export failed: {path}")
    for obj in selected:
        obj.select_set(False)
    return {
        "file": str(path.relative_to(EXPORTS)),
        "objects": len(selected),
        "bytes": path.stat().st_size,
        "compression": "none",
    }


def point_list(obj: bpy.types.Object) -> list[list[float]]:
    return [
        [round(vertex.co.x, 5), round(vertex.co.y, 5), round(vertex.co.z, 5)]
        for vertex in obj.data.vertices
    ]


def serialize_instances(scene_id: str, names: tuple[str, ...]) -> dict:
    groups = []
    for name in names:
        obj = bpy.data.objects[name]
        points = point_list(obj)
        groups.append({
            "id": name,
            "source_object": obj.get("scatter_source"),
            "count": len(points),
            "scale_range": [float(obj.get("min_scale", 0.0)), float(obj.get("max_scale", 0.0))],
            "rotation": "deterministic geometry-nodes Z randomization",
            "points_m": points,
        })
    return {
        "schema_version": "1.0.0",
        "scene_id": scene_id,
        "origin_m": [0.0, 0.0, 0.0],
        "groups": groups,
    }


def measured_slopes(points) -> list[float]:
    values = []
    for first, second in zip(points, points[1:]):
        horizontal = math.hypot(second[0] - first[0], second[1] - first[1])
        values.append(math.degrees(math.atan2(abs(second[2] - first[2]), horizontal)))
    return values


def serialize_routes(config, data, scene_id: str) -> dict:
    routes = {}
    for route_id, route in data.routes.items():
        points = build_route_polyline(data, config, route)
        slopes = measured_slopes(points)
        routes[route_id] = {
            **route,
            "points": [[round(value, 5) for value in point] for point in points],
            "measured_max_slope_deg": round(max(slopes, default=0.0), 6),
            "measured_mean_slope_deg": round(sum(slopes) / max(len(slopes), 1), 6),
            "surface_policy": "terrain-following with raised bridge/embankment profile",
        }
    return {
        "schema_version": "1.0.0",
        "scene_id": scene_id,
        "routes": routes,
    }


def serialize_materials() -> dict:
    items = []
    for material in sorted((item for item in bpy.data.materials if item.name.startswith("MAT_")), key=lambda item: item.name):
        images = []
        if material.node_tree:
            for node in material.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image:
                    images.append({
                        "name": node.image.name,
                        "packed": bool(node.image.packed_file),
                        "source": "CC0_PBR",
                    })
        items.append({
            "name": material.name,
            "source_license": "CC0-1.0" if images else "procedural_project_material",
            "images": images,
        })
    return {
        "schema_version": "1.0.0",
        "palette_direction": "serious natural PBR with controlled emerald teal ochre terracotta cobalt amber cyan violet accents",
        "materials": items,
    }


def serialize_assets() -> dict:
    scene = bpy.data.scenes["BIBLIOTECA_LOCAL"]
    assets = []
    for obj in sorted(scene.objects, key=lambda item: item.name):
        asset_type = obj.get("asset_type")
        if not asset_type:
            continue
        assets.append({
            "name": obj.name,
            "asset_type": asset_type,
            "production_level": obj.get("production_level", "medium_detail_base"),
            "vertices": len(obj.data.vertices) if obj.type == "MESH" else None,
            "materials": [slot.material.name for slot in obj.material_slots if slot.material],
        })
    return {
        "schema_version": "1.0.0",
        "scene_id": "BIBLIOTECA_LOCAL",
        "characters_or_animals_included": False,
        "assets": assets,
    }


def serialize_pois(scene: bpy.types.Scene) -> dict:
    pois = []
    seen = set()
    for obj in scene.objects:
        poi_id = obj.get("poi_id")
        if not poi_id or poi_id in seen:
            continue
        seen.add(poi_id)
        pois.append({
            "id": poi_id,
            "object": obj.name,
            "location_m": [round(float(value), 5) for value in obj.location],
        })
    return {"schema_version": "1.0.0", "scene_id": scene.name, "points_of_interest": sorted(pois, key=lambda item: item["id"])}


def main() -> None:
    if Path(bpy.data.filepath).name != "MUNDO_MESTRE.blend":
        raise RuntimeError("export_world.py must run with MUNDO_MESTRE.blend open")

    config = load_config(ROOT / "config/world_config.json")
    region_data = generate_world_data(config, "region")
    turbulent_data = generate_world_data(config, "turbulent")
    atlas_scene = bpy.data.scenes["ATLAS_MUNDO"]
    region_scene = bpy.data.scenes["REGIAO_COMERCIAL"]
    turbulent_scene = bpy.data.scenes["TURBULENTA_01"]
    library_scene = bpy.data.scenes["BIBLIOTECA_LOCAL"]
    exports = []

    exports.append(export_glb(EXPORTS / "atlas/atlas_mundo.glb", atlas_scene, renderable(atlas_scene.objects)))

    region_chunks = sorted(
        (obj for obj in region_scene.objects if obj.get("terrain_chunk")),
        key=lambda obj: obj.get("chunk_id"),
    )
    for chunk in region_chunks:
        exports.append(export_glb(EXPORTS / f"region/chunks/{chunk.get('chunk_id')}.glb", region_scene, [chunk]))

    routes = collection_renderables("REGION_ROUTES")
    architecture = collection_renderables("REGION_ARCHITECTURE")
    waterworks = collection_renderables("REGION_WATERWORKS")
    nature_sources = [
        obj for obj in library_scene.objects
        if obj.get("asset_type") in {"broadleaf_tree", "pine_tree", "rock", "grass"}
    ]
    river = [bpy.data.objects["REGION_MainRiver"]]
    exports.append(export_glb(EXPORTS / "region/routes.glb", region_scene, routes))
    exports.append(export_glb(EXPORTS / "region/architecture.glb", region_scene, architecture))
    exports.append(export_glb(EXPORTS / "region/waterworks.glb", region_scene, waterworks))
    # The dense distribution stays instanced in the master. The GLB contains
    # reusable source meshes and instances.json carries deterministic points.
    exports.append(export_glb(EXPORTS / "region/nature.glb", library_scene, nature_sources))
    exports.append(export_glb(EXPORTS / "region/environment.glb", region_scene, region_chunks + river + routes + architecture + waterworks))
    exports.append(export_glb(EXPORTS / "turbulent/turbulenta_01.glb", turbulent_scene, renderable(turbulent_scene.objects)))
    exports.append(export_glb(EXPORTS / "library/environment_kit.glb", library_scene, renderable(library_scene.objects)))

    json_write(EXPORTS / "region/instances.json", serialize_instances(
        "REGIAO_COMERCIAL",
        ("SCATTER_Broadleaf", "SCATTER_Pines", "SCATTER_Rocks", "SCATTER_Grass"),
    ))
    json_write(EXPORTS / "region/routes.json", serialize_routes(config, region_data, "REGIAO_COMERCIAL"))
    json_write(EXPORTS / "region/points_of_interest.json", serialize_pois(region_scene))
    json_write(EXPORTS / "turbulent/routes.json", serialize_routes(config, turbulent_data, "TURBULENTA_01"))
    json_write(EXPORTS / "turbulent/instances.json", serialize_instances(
        "TURBULENTA_01",
        ("TURBULENT_ScatterRocks",),
    ))
    json_write(EXPORTS / "turbulent/points_of_interest.json", serialize_pois(turbulent_scene))
    json_write(EXPORTS / "materials.json", serialize_materials())
    json_write(EXPORTS / "library/assets.json", serialize_assets())

    manifest_path = EXPORTS / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["export_files"] = sorted(item["file"] for item in exports) + [
        "library/assets.json",
        "materials.json",
        "region/instances.json",
        "region/points_of_interest.json",
        "region/routes.json",
        "turbulent/points_of_interest.json",
        "turbulent/instances.json",
        "turbulent/routes.json",
    ]
    manifest["scenes"]["REGIAO_COMERCIAL"]["functional_sets"] = {
        "environment": "region/environment.glb",
        "routes": "region/routes.glb",
        "architecture": "region/architecture.glb",
        "nature": "region/nature.glb",
        "waterworks": "region/waterworks.glb",
    }
    manifest["scenes"]["REGIAO_COMERCIAL"]["route_geometry"] = "region/routes.json"
    manifest["scenes"]["REGIAO_COMERCIAL"]["points_of_interest"] = "region/points_of_interest.json"
    manifest["scenes"]["TURBULENTA_01"]["route_geometry"] = "turbulent/routes.json"
    manifest["scenes"]["TURBULENTA_01"]["points_of_interest"] = "turbulent/points_of_interest.json"
    manifest["scenes"]["TURBULENTA_01"]["instances"] = "turbulent/instances.json"
    manifest["library"] = {
        "glb": "library/environment_kit.glb",
        "inventory": "library/assets.json",
        "materials": "materials.json",
    }
    manifest["export_policy"] = {
        "glb_compression": "uncompressed because Debian Blender package has no Draco shared library",
        "axis": "Y-up glTF",
        "master_coordinates_preserved": True,
        "demo_consumes_manifest": False,
    }
    json_write(manifest_path, manifest)
    json_write(ROOT / "reports/export-report.json", {
        "schema_version": "1.0.0",
        "blender_version": bpy.app.version_string,
        "exports": exports,
        "total_glb_bytes": sum(item["bytes"] for item in exports),
    })
    print("WORLD_EXPORT_OK", len(exports), sum(item["bytes"] for item in exports))


if __name__ == "__main__":
    main()
