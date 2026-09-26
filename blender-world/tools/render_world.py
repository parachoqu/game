"""Render the twelve validation views and three final 4K images."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time

import bpy


TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent


VALIDATION = [
    ("ATLAS_MUNDO", "CAM_01_ATLAS_FISICO", "01_atlas_fisico.png"),
    ("ATLAS_MUNDO", "CAM_02_ATLAS_POLITICO", "02_atlas_politico.png"),
    ("ATLAS_MUNDO", "CAM_03_CONTINENTE_PRINCIPAL", "03_continente_principal.png"),
    ("ATLAS_MUNDO", "CAM_04_SEGUNDO_CONTINENTE", "04_segundo_continente.png"),
    ("REGIAO_COMERCIAL", "CAM_05_REGION_AEREA", "05_regiao_aerea.png"),
    ("REGIAO_COMERCIAL", "CAM_06_VALE_ENTREPOSTO", "06_vale_entreposto.png"),
    ("REGIAO_COMERCIAL", "CAM_07_BOSQUE_MINA", "07_bosque_mina.png"),
    ("REGIAO_COMERCIAL", "CAM_08_GARGANTA", "08_garganta.png"),
    ("REGIAO_COMERCIAL", "CAM_09_ACAMPAMENTO_OBSERVATORIO", "09_acampamento_observatorio.png"),
    ("REGIAO_COMERCIAL", "CAM_10_ERMOS_ENTREPOSTO_NORTE", "10_ermos_entreposto_norte.png"),
    ("TURBULENTA_01", "CAM_11_TURBULENTA_AEREA", "11_turbulenta_aerea.png"),
    ("TURBULENTA_01", "CAM_12_TURBULENTA_PORTAL", "12_turbulenta_portal.png"),
]

FINALS = [
    ("ATLAS_MUNDO", "CAM_03_CONTINENTE_PRINCIPAL", "final_atlas.png"),
    ("REGIAO_COMERCIAL", "CAM_06_VALE_ENTREPOSTO", "final_regiao_comercial.png"),
    ("TURBULENTA_01", "CAM_12_TURBULENTA_PORTAL", "final_turbulenta.png"),
]


def parse_args():
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", action="store_true")
    parser.add_argument("--only", nargs="*", default=[])
    return parser.parse_args(values)


def hip_probe() -> dict:
    result = {"available": False, "devices": [], "reason": "HIP runtime not exposed by Cycles"}
    addon = bpy.context.preferences.addons.get("cycles")
    if not addon:
        result["reason"] = "Cycles addon unavailable"
        return result
    try:
        addon.preferences.get_devices()
        result["devices"] = [
            {"name": device.name, "type": device.type}
            for device in addon.preferences.devices
        ]
        result["available"] = any(device.type == "HIP" for device in addon.preferences.devices)
        if result["available"]:
            result["reason"] = "HIP device detected; production reliability test still required"
    except Exception as error:
        result["reason"] = f"Cycles device probe failed: {error}"
    return result


def configure(scene: bpy.types.Scene, width: int, height: int, engine: str) -> None:
    scene.render.engine = engine
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.image_settings.color_depth = "8"
    scene.render.film_transparent = False
    scene.render.use_file_extension = True
    scene.render.use_overwrite = True


def set_atlas_layer(camera_name: str) -> None:
    political = bpy.data.collections.get("ATLAS_POLITICAL")
    if political:
        political.hide_render = camera_name != "CAM_02_ATLAS_POLITICO"


def render_one(scene_name: str, camera_name: str, path: Path, width: int, height: int, engine: str) -> dict:
    scene = bpy.data.scenes[scene_name]
    camera = bpy.data.objects[camera_name]
    bpy.context.window.scene = scene
    scene.camera = camera
    configure(scene, width, height, engine)
    set_atlas_layer(camera_name)
    path.parent.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(path)
    started = time.perf_counter()
    bpy.ops.render.render(write_still=True)
    elapsed = time.perf_counter() - started
    if not path.is_file():
        raise RuntimeError(f"render was not written: {path}")
    return {
        "scene": scene_name,
        "camera": camera_name,
        "file": str(path.relative_to(ROOT)),
        "resolution": [width, height],
        "engine": engine,
        "seconds": round(elapsed, 3),
        "bytes": path.stat().st_size,
    }


def main() -> None:
    if Path(bpy.data.filepath).name != "MUNDO_MESTRE.blend":
        raise RuntimeError("render_world.py must run with MUNDO_MESTRE.blend open")
    args = parse_args()
    hip = hip_probe()
    # HIP is not available in the Kali Blender build on this workstation, so
    # the approved fallback is Eevee for both validation and final images.
    final_engine = "BLENDER_EEVEE"

    if args.preview:
        output = ROOT / "renders/previews"
        width, height = 960, 540
        selected = [entry for entry in VALIDATION if not args.only or entry[1] in args.only]
        rendered = [
            render_one(entry[0], entry[1], output / entry[2], width, height, "BLENDER_EEVEE")
            for entry in selected
        ]
        print("PREVIEW_RENDER_OK", len(rendered))
        return

    validation_output = ROOT / "renders/validation"
    final_output = ROOT / "renders/final"
    validation_output.mkdir(parents=True, exist_ok=True)
    final_output.mkdir(parents=True, exist_ok=True)
    if not args.only:
        for old in validation_output.glob("*.png"):
            old.unlink()
        for old in final_output.glob("*.png"):
            old.unlink()

    selected_validation = [entry for entry in VALIDATION if not args.only or entry[1] in args.only]
    selected_finals = [entry for entry in FINALS if not args.only or entry[1] in args.only]

    validation_records = [
        render_one(entry[0], entry[1], validation_output / entry[2], 2560, 1440, "BLENDER_EEVEE")
        for entry in selected_validation
    ]
    final_records = [
        render_one(entry[0], entry[1], final_output / entry[2], 3840, 2160, final_engine)
        for entry in selected_finals
    ]
    report_path = ROOT / "reports/render-report.json"
    if args.only and report_path.is_file():
        previous = json.loads(report_path.read_text(encoding="utf-8"))
        validation_by_camera = {item["camera"]: item for item in previous.get("validation_renders", [])}
        final_by_camera = {item["camera"]: item for item in previous.get("final_renders", [])}
        validation_by_camera.update({item["camera"]: item for item in validation_records})
        final_by_camera.update({item["camera"]: item for item in final_records})
        validation_records = [validation_by_camera[entry[1]] for entry in VALIDATION if entry[1] in validation_by_camera]
        final_records = [final_by_camera[entry[1]] for entry in FINALS if entry[1] in final_by_camera]
    report = {
        "blender_version": bpy.app.version_string,
        "hip_probe": hip,
        "final_engine": final_engine,
        "fallback_reason": hip["reason"],
        "resolution_validation": [2560, 1440],
        "resolution_final": [3840, 2160],
        "validation_renders": validation_records,
        "final_renders": final_records,
    }
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("FULL_RENDER_OK", len(validation_records), len(final_records), final_engine)


if __name__ == "__main__":
    main()
