import json
import unittest
from pathlib import Path

from PIL import Image, ImageStat


ROOT = Path(__file__).resolve().parents[1]


class RenderContractTests(unittest.TestCase):
    def test_twelve_validation_renders_have_required_resolution(self):
        validation = ROOT / "renders/validation"
        expected = [
            "01_atlas_fisico.png",
            "02_atlas_politico.png",
            "03_continente_principal.png",
            "04_segundo_continente.png",
            "05_regiao_aerea.png",
            "06_vale_entreposto.png",
            "07_bosque_mina.png",
            "08_garganta.png",
            "09_acampamento_observatorio.png",
            "10_ermos_entreposto_norte.png",
            "11_turbulenta_aerea.png",
            "12_turbulenta_portal.png",
        ]
        self.assertEqual(sorted(path.name for path in validation.glob("*.png")), expected)
        for name in expected:
            with self.subTest(name=name):
                image = Image.open(validation / name).convert("RGB")
                self.assertEqual(image.size, (2560, 1440))
                extrema = ImageStat.Stat(image).extrema
                self.assertTrue(any(high - low > 20 for low, high in extrema), extrema)

    def test_three_final_renders_are_4k(self):
        final = ROOT / "renders/final"
        expected = {
            "final_atlas.png",
            "final_regiao_comercial.png",
            "final_turbulenta.png",
        }
        self.assertEqual({path.name for path in final.glob("*.png")}, expected)
        for name in expected:
            image = Image.open(final / name)
            self.assertEqual(image.size, (3840, 2160))

    def test_render_report_records_engine_and_camera(self):
        report = json.loads((ROOT / "reports/render-report.json").read_text(encoding="utf-8"))
        self.assertEqual(len(report["validation_renders"]), 12)
        self.assertEqual(len(report["final_renders"]), 3)
        self.assertIn(report["final_engine"], {"BLENDER_EEVEE", "CYCLES"})
        self.assertEqual(report["resolution_validation"], [2560, 1440])
        self.assertEqual(report["resolution_final"], [3840, 2160])


if __name__ == "__main__":
    unittest.main()
