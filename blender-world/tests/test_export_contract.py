import json
import struct
import unittest
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]


def glb_document(path: Path) -> dict:
    payload = path.read_bytes()
    magic, version, length = struct.unpack_from("<4sII", payload, 0)
    if magic != b"glTF" or version != 2 or length != len(payload):
        raise AssertionError(f"invalid GLB header: {path}")
    chunk_length, chunk_type = struct.unpack_from("<II", payload, 12)
    if chunk_type != 0x4E4F534A:
        raise AssertionError(f"first GLB chunk is not JSON: {path}")
    return json.loads(payload[20 : 20 + chunk_length].decode("utf-8"))


class ExportContractTests(unittest.TestCase):
    def test_required_glbs_exist_and_have_binary_gltf_header(self):
        expected = [
            ROOT / "exports/atlas/atlas_mundo.glb",
            ROOT / "exports/region/environment.glb",
            ROOT / "exports/region/routes.glb",
            ROOT / "exports/region/architecture.glb",
            ROOT / "exports/region/nature.glb",
            ROOT / "exports/region/waterworks.glb",
            ROOT / "exports/turbulent/turbulenta_01.glb",
            ROOT / "exports/library/environment_kit.glb",
        ]
        expected.extend(
            ROOT / f"exports/region/chunks/region_{col}_{row}.glb"
            for row in range(4)
            for col in range(4)
        )
        for path in expected:
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertTrue(path.is_file())
                self.assertGreater(path.stat().st_size, 1024)
                self.assertEqual(path.read_bytes()[:4], b"glTF")

    def test_instances_and_routes_are_serialized(self):
        instances = json.loads((ROOT / "exports/region/instances.json").read_text(encoding="utf-8"))
        self.assertEqual(instances["schema_version"], "1.0.0")
        self.assertGreaterEqual(len(instances["groups"]), 4)
        self.assertGreater(sum(group["count"] for group in instances["groups"]), 2000)

        turbulent_instances = json.loads((ROOT / "exports/turbulent/instances.json").read_text(encoding="utf-8"))
        self.assertEqual(turbulent_instances["scene_id"], "TURBULENTA_01")
        self.assertEqual(len(turbulent_instances["groups"]), 1)
        self.assertGreater(turbulent_instances["groups"][0]["count"], 20)

        routes = json.loads((ROOT / "exports/region/routes.json").read_text(encoding="utf-8"))
        self.assertEqual(set(routes["routes"]), {
            "short_gorge", "safe_east", "mine_spur", "observatory_trail",
        })
        for route in routes["routes"].values():
            self.assertGreater(len(route["points"]), 10)
            self.assertLessEqual(route["measured_max_slope_deg"], route["max_sustained_slope_deg"] + 1e-4)

    def test_each_chunk_glb_is_isolated_from_other_scenes(self):
        for row in range(4):
            for col in range(4):
                chunk_id = f"region_{col}_{row}"
                document = glb_document(ROOT / f"exports/region/chunks/{chunk_id}.glb")
                names = {node.get("name", "") for node in document.get("nodes", [])}
                self.assertIn(f"REGION_Terrain_{col}_{row}", names)
                self.assertFalse(any(name.startswith("ATLAS_") for name in names), names)
                self.assertFalse(any(name.startswith("REGION_Terrain_") and name != f"REGION_Terrain_{col}_{row}" for name in names), names)

    def test_nature_glb_contains_reusable_sources(self):
        document = glb_document(ROOT / "exports/region/nature.glb")
        names = {node.get("name", "") for node in document.get("nodes", [])}
        self.assertTrue({
            "ASSET_BroadleafTree", "ASSET_PineTree", "ASSET_Rock", "ASSET_GrassClump",
        }.issubset(names), names)

    def test_manifest_paths_resolve(self):
        manifest = json.loads((ROOT / "exports/manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema_version"], "1.0.0")
        for relative in manifest["export_files"]:
            self.assertTrue((ROOT / "exports" / relative).is_file(), relative)

    def test_height_pngs_are_sixteen_bit(self):
        for kind in ("atlas", "region", "turbulent"):
            image = Image.open(ROOT / f"exports/maps/{kind}_height.png")
            self.assertIn(image.mode, {"I;16", "I;16B", "I"})
            extrema = image.getextrema()
            self.assertGreater(extrema[1], extrema[0])


if __name__ == "__main__":
    unittest.main()
