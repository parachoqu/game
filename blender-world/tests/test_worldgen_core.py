import copy
import hashlib
import json
import sys
import unittest
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from worldgen_core import (  # noqa: E402
    build_route_polyline,
    build_export_manifest,
    chunk_heightfield,
    count_landmasses,
    flow_accumulation_d8,
    generate_world_data,
    load_config,
    validate_config,
)


def array_digest(value: np.ndarray) -> str:
    return hashlib.sha256(np.ascontiguousarray(value).tobytes()).hexdigest()


class WorldConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config_path = ROOT / "config" / "world_config.json"
        cls.config = load_config(cls.config_path)

    def test_config_matches_approved_contract(self):
        validate_config(self.config)
        self.assertEqual(self.config["blender_version"], "5.0.1")
        self.assertEqual(self.config["seeds"], {
            "atlas": 26092401,
            "region": 26092402,
            "turbulent": 26092403,
        })
        self.assertEqual(self.config["atlas"]["size_km"], [220, 140])
        self.assertEqual(self.config["region"]["size_m"], [1024, 1024])
        self.assertEqual(self.config["region"]["samples"], [1025, 1025])
        self.assertEqual(self.config["region"]["chunks"], [4, 4])
        self.assertEqual(self.config["turbulent"]["size_m"], [256, 256])
        self.assertEqual(self.config["turbulent"]["samples"], [513, 513])
        self.assertEqual(len(self.config["realms"]), 6)
        self.assertTrue(all(realm["status"] == "PROPOSTA" for realm in self.config["realms"]))
        self.assertEqual(self.config["biome_rules"]["inputs"], [
            "altitude", "slope", "moisture", "exposure", "distance_to_water", "occupation",
        ])
        self.assertEqual(self.config["distribution"]["region"]["broadleaf_count"], 760)
        self.assertEqual(len(self.config["hydrology"]["region_river_crossings_m"]), 4)

    def test_invalid_resolution_is_rejected(self):
        invalid = copy.deepcopy(self.config)
        invalid["region"]["samples"] = [1024, 1024]
        with self.assertRaisesRegex(ValueError, "chunks.*shared borders"):
            validate_config(invalid)


class TerrainGenerationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = load_config(ROOT / "config" / "world_config.json")

    def _small_config(self):
        config = copy.deepcopy(self.config)
        config["atlas"]["samples"] = [221, 141]
        config["region"]["samples"] = [129, 129]
        config["region"]["chunks"] = [4, 4]
        config["turbulent"]["samples"] = [129, 129]
        return config

    def test_generation_is_deterministic(self):
        config = self._small_config()
        first = generate_world_data(config, "region")
        second = generate_world_data(config, "region")
        self.assertEqual(array_digest(first.height), array_digest(second.height))
        self.assertEqual(array_digest(first.biome), array_digest(second.biome))
        self.assertEqual(first.routes, second.routes)

    def test_atlas_contains_two_separate_major_landmasses(self):
        atlas = generate_world_data(self._small_config(), "atlas")
        self.assertEqual(count_landmasses(atlas.land, min_cells=400), 2)
        self.assertGreater(np.count_nonzero(atlas.land), atlas.land.size * 0.18)
        self.assertLess(np.count_nonzero(atlas.land), atlas.land.size * 0.62)

    def test_region_chunks_share_exact_border_samples(self):
        config = self._small_config()
        region = generate_world_data(config, "region")
        chunks = chunk_heightfield(region.height, 4, 4)
        self.assertEqual(len(chunks), 16)
        for row in range(4):
            for col in range(3):
                left = chunks[(col, row)]
                right = chunks[(col + 1, row)]
                np.testing.assert_array_equal(left[:, -1], right[:, 0])
        for row in range(3):
            for col in range(4):
                bottom = chunks[(col, row)]
                top = chunks[(col, row + 1)]
                np.testing.assert_array_equal(bottom[-1, :], top[0, :])

    def test_routes_connect_every_required_region_anchor(self):
        region = generate_world_data(self._small_config(), "region")
        required = {
            "outpost_south", "outpost_north", "mine", "camp",
            "observatory", "wastes", "portal_turbulent",
        }
        touched = {name for route in region.routes.values() for name in route["anchors"]}
        self.assertTrue(required.issubset(touched))
        self.assertGreaterEqual(region.routes["safe_east"]["width_m"], 4.0)
        self.assertLessEqual(region.routes["safe_east"]["max_sustained_slope_deg"], 12.0)
        self.assertLessEqual(region.routes["short_gorge"]["max_peak_slope_deg"], 28.0)

    def test_route_geometry_respects_declared_sustained_grade(self):
        config = self._small_config()
        region = generate_world_data(config, "region")
        for route in region.routes.values():
            points = build_route_polyline(region, config, route)
            measured = []
            for first, second in zip(points, points[1:]):
                horizontal = np.hypot(second[0] - first[0], second[1] - first[1])
                measured.append(np.degrees(np.arctan2(abs(second[2] - first[2]), horizontal)))
            self.assertLessEqual(
                max(measured),
                route["max_sustained_slope_deg"] + 1e-5,
            )

    def test_region_river_is_continuous_to_both_map_edges(self):
        region = generate_world_data(self._small_config(), "region")
        self.assertGreater(np.count_nonzero(region.water[:, 0]), 0)
        self.assertGreater(np.count_nonzero(region.water[:, -1]), 0)
        self.assertEqual(count_landmasses(region.water, min_cells=1), 1)

    def test_turbulent_has_entry_and_two_distinct_exits(self):
        turbulent = generate_world_data(self._small_config(), "turbulent")
        self.assertIn("entry", turbulent.anchors)
        exits = [name for name in turbulent.anchors if name.startswith("exit_")]
        self.assertEqual(len(exits), 2)
        self.assertNotEqual(turbulent.anchors[exits[0]], turbulent.anchors[exits[1]])

    def test_d8_accumulation_grows_downhill(self):
        height = np.array([
            [9.0, 8.0, 7.0],
            [8.0, 6.0, 4.0],
            [7.0, 4.0, 0.0],
        ])
        accumulation, receiver = flow_accumulation_d8(height)
        self.assertEqual(tuple(receiver[0, 0]), (1, 1))
        self.assertEqual(tuple(receiver[1, 1]), (2, 2))
        self.assertEqual(receiver[2, 2, 0], -1)
        self.assertEqual(accumulation[2, 2], 9.0)

    def test_manifest_has_future_export_contract(self):
        config = self._small_config()
        region = generate_world_data(config, "region")
        manifest = build_export_manifest(config, {"region": region})
        scene = manifest["scenes"]["REGIAO_COMERCIAL"]
        self.assertEqual(scene["unit_meters"], 1.0)
        self.assertEqual(scene["bounds_m"], [-512.0, -512.0, 512.0, 512.0])
        self.assertEqual(len(scene["chunks"]), 16)
        self.assertIn("heightmap_exr", scene)
        self.assertIn("routes", scene)
        json.dumps(manifest, ensure_ascii=False)


if __name__ == "__main__":
    unittest.main()
