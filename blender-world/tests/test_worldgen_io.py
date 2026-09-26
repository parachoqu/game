import copy
import json
import sys
import tempfile
import unittest
import warnings
from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from worldgen_core import load_config  # noqa: E402
from worldgen_io import quantize_height_u16, write_world_bundle  # noqa: E402


class WorldBundleTests(unittest.TestCase):
    def setUp(self):
        self.config = load_config(ROOT / "config" / "world_config.json")
        self.config = copy.deepcopy(self.config)
        self.config["atlas"]["samples"] = [111, 71]
        self.config["region"]["samples"] = [65, 65]
        self.config["region"]["chunks"] = [4, 4]
        self.config["turbulent"]["samples"] = [65, 65]

    def test_quantize_height_uses_full_uint16_range(self):
        source = np.array([[-2.0, 0.0], [1.0, 6.0]], dtype=np.float32)
        encoded, metadata = quantize_height_u16(source)
        self.assertEqual(encoded.dtype, np.uint16)
        self.assertEqual(int(encoded.min()), 0)
        self.assertEqual(int(encoded.max()), 65535)
        self.assertEqual(metadata, {"min": -2.0, "max": 6.0, "encoding": "linear_uint16"})

    def test_png16_generation_emits_no_deprecation_warning(self):
        with tempfile.TemporaryDirectory() as temporary:
            with warnings.catch_warnings(record=True) as captured:
                warnings.simplefilter("always")
                write_world_bundle(self.config, Path(temporary))
            deprecated = [item for item in captured if issubclass(item.category, DeprecationWarning)]
            self.assertEqual(deprecated, [])

    def test_write_bundle_creates_manifest_maps_and_determinism_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = write_world_bundle(self.config, root)
            second = write_world_bundle(self.config, root)
            self.assertEqual(first["hashes"], second["hashes"])
            manifest = json.loads((root / "exports" / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(set(manifest["scenes"]), {
                "ATLAS_MUNDO", "REGIAO_COMERCIAL", "TURBULENTA_01",
            })
            self.assertEqual(manifest["scenes"]["REGIAO_COMERCIAL"]["chunks"][0]["id"], "region_0_0")
            for name in (
                "atlas_height.png", "atlas_biome.png", "region_height.png",
                "region_biome.png", "region_water.png", "turbulent_height.png",
            ):
                path = root / "exports" / "maps" / name
                self.assertTrue(path.is_file(), name)
                with Image.open(path) as image:
                    self.assertGreater(image.width, 1)
                    self.assertGreater(image.height, 1)
            report = json.loads((root / "reports" / "generation-report.json").read_text(encoding="utf-8"))
            self.assertTrue(report["deterministic"])
            self.assertEqual(report["seeds"]["region"], 26092402)


if __name__ == "__main__":
    unittest.main()
