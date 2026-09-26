from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SCENES = {
    "ATLAS_MUNDO",
    "REGIAO_COMERCIAL",
    "TURBULENTA_01",
    "BIBLIOTECA_LOCAL",
    "VALIDACAO",
}
EXPECTED_WORKFLOW_COLLECTIONS = {
    "FONTES_PROCEDURAIS",
    "BASE_EDITAVEL",
    "ACABAMENTO_MANUAL",
    "PREPARACAO_EXPORTACAO",
}

assert Path(bpy.data.filepath).name == "MUNDO_MESTRE.blend", bpy.data.filepath
scene_names = {scene.name for scene in bpy.data.scenes}
collection_names = {collection.name for collection in bpy.data.collections}
assert scene_names == EXPECTED_SCENES, scene_names
assert EXPECTED_WORKFLOW_COLLECTIONS.issubset(collection_names), collection_names
generation_collections = {
    name for name in collection_names if name.startswith("GEN_") and "__seed_" in name
}
assert generation_collections == {
    "GEN_ATLAS__seed_26092401__v001",
    "GEN_REGION__seed_26092402__v001",
    "GEN_TURBULENT__seed_26092403__v001",
}, generation_collections

atlas = bpy.data.scenes["ATLAS_MUNDO"]
region = bpy.data.scenes["REGIAO_COMERCIAL"]
turbulent = bpy.data.scenes["TURBULENTA_01"]
library = bpy.data.scenes["BIBLIOTECA_LOCAL"]
validation = bpy.data.scenes["VALIDACAO"]

assert atlas.get("scale_contract") == "1 BU = 1 km"
assert region.get("scale_contract") == "1 BU = 1 m"
assert turbulent.get("scale_contract") == "1 BU = 1 m"
assert atlas.get("world_schema_version") == "1.0.0"
assert {"ATLAS_POLITICAL", "ATLAS_RISK", "ATLAS_ANOMALIES"}.issubset(collection_names)

region_chunks = [obj for obj in region.objects if obj.get("terrain_chunk")]
assert len(region_chunks) == 16, len(region_chunks)
assert {obj.get("chunk_id") for obj in region_chunks} == {
    f"region_{col}_{row}" for row in range(4) for col in range(4)
}

required_pois = {
    "outpost_south", "outpost_north", "mine", "cave_west", "camp", "observatory",
    "wastes", "portal_turbulent",
}
poi_ids = {obj.get("poi_id") for obj in region.objects if obj.get("poi_id")}
assert required_pois.issubset(poi_ids), poi_ids

route_ids = {obj.get("route_id") for obj in region.objects if obj.get("route_id")}
assert {"short_gorge", "safe_east", "mine_spur", "observatory_trail"}.issubset(route_ids)
facility_types = {obj.get("facility_type") for obj in region.objects if obj.get("facility_type")}
assert {
    "workshop_exterior", "warehouse_exterior", "stable_exterior",
    "productive_field", "productive_waterwork", "cave_exterior",
}.issubset(facility_types), facility_types

exit_ids = {obj.get("anchor_id") for obj in turbulent.objects if obj.get("anchor_id")}
assert {"entry", "exit_east", "exit_west"}.issubset(exit_ids)
assert any(obj.get("anomaly_portal") for obj in turbulent.objects)

asset_types = {obj.get("asset_type") for obj in library.objects if obj.get("asset_type")}
assert {
    "broadleaf_tree", "pine_tree", "dead_tree", "shrub", "grass",
    "rock", "bridge", "dock", "wall", "arch", "roof", "tower",
    "tent", "market_stall", "mine", "observatory", "camp", "ruin",
}.issubset(asset_types), asset_types

assert len([material for material in bpy.data.materials if material.name.startswith("MAT_")]) >= 15
assert len([group for group in bpy.data.node_groups if group.name.startswith("GN_SCATTER_")]) >= 3
assert len([obj for obj in bpy.data.objects if obj.type == "CAMERA" and obj.get("validation_camera")]) == 12
assert len(validation.objects) >= 12

for image in bpy.data.images:
    if image.source == "FILE":
        assert image.packed_file or image.filepath.startswith("//"), (image.name, image.filepath)

print(
    "BLENDER_STRUCTURE_OK",
    len(bpy.data.objects),
    len(bpy.data.materials),
    len(region_chunks),
)
