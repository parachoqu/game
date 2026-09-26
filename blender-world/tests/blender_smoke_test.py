from pathlib import Path
import sys

import bpy
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "reports" / "blender-smoke"
OUTPUT.mkdir(parents=True, exist_ok=True)

assert bpy.app.version[:3] == (5, 0, 1), bpy.app.version_string
assert np.__version__

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.name = "SMOKE_TEST"
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x = 320
scene.render.resolution_y = 180
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(OUTPUT / "smoke.png")

bpy.ops.mesh.primitive_cube_add(location=(0.0, 0.0, 0.0))
cube = bpy.context.object
cube.name = "SmokeCube"

material = bpy.data.materials.new("SmokeMaterial")
material.diffuse_color = (0.08, 0.48, 0.34, 1.0)
cube.data.materials.append(material)

bpy.ops.object.camera_add(location=(5.5, -5.5, 4.5))
camera = bpy.context.object
scene.camera = camera
direction = cube.location - camera.location
camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()

bpy.ops.object.light_add(type="AREA", location=(3.0, -2.0, 6.0))
light = bpy.context.object
light.data.energy = 900.0
light.data.shape = "DISK"
light.data.size = 5.0

bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT / "smoke.blend"))

bpy.ops.object.select_all(action="DESELECT")
cube.select_set(True)
bpy.context.view_layer.objects.active = cube
bpy.ops.export_scene.gltf(
    filepath=str(OUTPUT / "smoke.glb"),
    export_format="GLB",
    use_selection=True,
    export_apply=True,
)
bpy.ops.render.render(write_still=True)

for expected in ("smoke.blend", "smoke.glb", "smoke.png"):
    path = OUTPUT / expected
    assert path.is_file() and path.stat().st_size > 0, path

print("BLENDER_SMOKE_OK", bpy.app.version_string, np.__version__)
