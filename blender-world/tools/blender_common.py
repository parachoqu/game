"""Blender helpers for the procedural world master file."""

from __future__ import annotations

import math
from pathlib import Path
from typing import Iterable, Sequence

import bpy
import bmesh
import numpy as np
from mathutils import Vector


def hex_rgba(value: str, alpha: float = 1.0) -> tuple[float, float, float, float]:
    value = value.lstrip("#")
    return tuple(int(value[index : index + 2], 16) / 255.0 for index in (0, 2, 4)) + (alpha,)


def set_principled_input(node, names: Sequence[str], value) -> None:
    for name in names:
        socket = node.inputs.get(name)
        if socket is not None:
            socket.default_value = value
            return


def make_material(
    name: str,
    color: Sequence[float],
    roughness: float = 0.65,
    metallic: float = 0.0,
    emission: Sequence[float] | None = None,
    emission_strength: float = 0.0,
    alpha: float = 1.0,
) -> bpy.types.Material:
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    principled = nodes.new("ShaderNodeBsdfPrincipled")
    set_principled_input(principled, ("Base Color",), tuple(color[:3]) + (1.0,))
    set_principled_input(principled, ("Roughness",), roughness)
    set_principled_input(principled, ("Metallic",), metallic)
    set_principled_input(principled, ("Alpha",), alpha)
    if emission is not None:
        set_principled_input(principled, ("Emission Color", "Emission"), tuple(emission[:3]) + (1.0,))
        set_principled_input(principled, ("Emission Strength",), emission_strength)
    material.node_tree.links.new(principled.outputs["BSDF"], output.inputs["Surface"])
    material.diffuse_color = tuple(color[:3]) + (alpha,)
    if alpha < 1.0:
        try:
            material.surface_render_method = "DITHERED"
        except Exception:
            pass
    return material


def make_vertex_color_material(name: str, roughness: float = 0.82) -> bpy.types.Material:
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    principled = nodes.new("ShaderNodeBsdfPrincipled")
    vertex_color = nodes.new("ShaderNodeVertexColor")
    vertex_color.layer_name = "BiomeColor"
    noise = nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 3.5
    noise.inputs["Detail"].default_value = 2.0
    noise.inputs["Roughness"].default_value = 0.68
    bump = nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.18
    bump.inputs["Distance"].default_value = 0.25
    set_principled_input(principled, ("Roughness",), roughness)
    material.node_tree.links.new(vertex_color.outputs["Color"], principled.inputs["Base Color"])
    material.node_tree.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    material.node_tree.links.new(bump.outputs["Normal"], principled.inputs["Normal"])
    material.node_tree.links.new(principled.outputs["BSDF"], output.inputs["Surface"])
    return material


def _load_image(path: Path, non_color: bool = False) -> bpy.types.Image:
    path = path.resolve()
    image = bpy.data.images.get(path.name)
    if image is None:
        image = bpy.data.images.load(str(path), check_existing=True)
    if non_color:
        try:
            image.colorspace_settings.name = "Non-Color"
        except TypeError:
            pass
    return image


def make_pbr_image_material(
    name: str,
    diffuse_path: Path,
    roughness_path: Path | None,
    normal_path: Path | None,
    tint: Sequence[float] = (1.0, 1.0, 1.0, 1.0),
    scale: float = 3.0,
) -> bpy.types.Material:
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    links = material.node_tree.links
    output = nodes.new("ShaderNodeOutputMaterial")
    principled = nodes.new("ShaderNodeBsdfPrincipled")
    texcoord = nodes.new("ShaderNodeTexCoord")
    mapping = nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (scale, scale, scale)
    links.new(texcoord.outputs["Generated"], mapping.inputs["Vector"])

    diffuse = nodes.new("ShaderNodeTexImage")
    diffuse.image = _load_image(diffuse_path)
    diffuse.projection = "BOX"
    diffuse.projection_blend = 0.25
    links.new(mapping.outputs["Vector"], diffuse.inputs["Vector"])
    multiply = nodes.new("ShaderNodeMixRGB")
    multiply.blend_type = "MULTIPLY"
    multiply.inputs[0].default_value = 1.0
    multiply.inputs[2].default_value = tuple(tint)
    links.new(diffuse.outputs["Color"], multiply.inputs[1])
    links.new(multiply.outputs["Color"], principled.inputs["Base Color"])

    if roughness_path is not None and roughness_path.is_file():
        roughness = nodes.new("ShaderNodeTexImage")
        roughness.image = _load_image(roughness_path, non_color=True)
        roughness.projection = "BOX"
        roughness.projection_blend = 0.25
        links.new(mapping.outputs["Vector"], roughness.inputs["Vector"])
        links.new(roughness.outputs["Color"], principled.inputs["Roughness"])
    else:
        set_principled_input(principled, ("Roughness",), 0.72)

    if normal_path is not None and normal_path.is_file():
        normal_tex = nodes.new("ShaderNodeTexImage")
        normal_tex.image = _load_image(normal_path, non_color=True)
        normal_tex.projection = "BOX"
        normal_tex.projection_blend = 0.25
        normal_map = nodes.new("ShaderNodeNormalMap")
        normal_map.inputs["Strength"].default_value = 0.42
        links.new(mapping.outputs["Vector"], normal_tex.inputs["Vector"])
        links.new(normal_tex.outputs["Color"], normal_map.inputs["Color"])
        links.new(normal_map.outputs["Normal"], principled.inputs["Normal"])

    links.new(principled.outputs["BSDF"], output.inputs["Surface"])
    return material


def new_collection(name: str, scene: bpy.types.Scene | None = None, parent: bpy.types.Collection | None = None) -> bpy.types.Collection:
    collection = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    if parent is not None and collection.name not in parent.children:
        parent.children.link(collection)
    elif scene is not None and collection.name not in scene.collection.children:
        scene.collection.children.link(collection)
    return collection


def move_object_to_collection(obj: bpy.types.Object, collection: bpy.types.Collection) -> None:
    for current in tuple(obj.users_collection):
        current.objects.unlink(obj)
    if obj.name not in collection.objects:
        collection.objects.link(obj)


def make_mesh_object(name: str, collection: bpy.types.Collection, vertices, faces) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def create_grid_mesh(
    name: str,
    height: np.ndarray,
    bounds: tuple[float, float, float, float],
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    colors: np.ndarray | None = None,
    target_samples: tuple[int, int] | None = None,
) -> bpy.types.Object:
    rows, cols = height.shape
    target_cols, target_rows = target_samples or (cols, rows)
    col_indices = np.linspace(0, cols - 1, target_cols).round().astype(np.int32)
    row_indices = np.linspace(0, rows - 1, target_rows).round().astype(np.int32)
    sampled = height[np.ix_(row_indices, col_indices)]
    min_x, min_y, max_x, max_y = bounds
    xs = np.linspace(min_x, max_x, target_cols)
    ys = np.linspace(min_y, max_y, target_rows)
    xx, yy = np.meshgrid(xs, ys)
    vertices = np.column_stack((xx.ravel(), yy.ravel(), sampled.ravel())).tolist()
    faces = []
    for row in range(target_rows - 1):
        start = row * target_cols
        next_start = (row + 1) * target_cols
        for col in range(target_cols - 1):
            faces.append((start + col, start + col + 1, next_start + col + 1, next_start + col))
    obj = make_mesh_object(name, collection, vertices, faces)
    obj.data.materials.append(material)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    if colors is not None:
        sampled_colors = colors[np.ix_(row_indices, col_indices)].reshape(-1, 4).astype(np.float32)
        attribute = obj.data.color_attributes.new(name="BiomeColor", type="FLOAT_COLOR", domain="POINT")
        attribute.data.foreach_set("color", sampled_colors.ravel())
    return obj


def create_ribbon(
    name: str,
    points: Sequence[Sequence[float]],
    width: float,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
) -> bpy.types.Object:
    if len(points) < 2:
        raise ValueError("a ribbon needs at least two points")
    left = []
    right = []
    for index, point in enumerate(points):
        current = Vector(point)
        if index == 0:
            tangent = Vector(points[1]) - current
        elif index == len(points) - 1:
            tangent = current - Vector(points[index - 1])
        else:
            tangent = Vector(points[index + 1]) - Vector(points[index - 1])
        tangent.z = 0.0
        tangent.normalize()
        normal = Vector((-tangent.y, tangent.x, 0.0)) * (width * 0.5)
        left.append(tuple(current + normal))
        right.append(tuple(current - normal))
    vertices = []
    for first, second in zip(left, right):
        vertices.extend((first, second))
    faces = [(index * 2, index * 2 + 1, index * 2 + 3, index * 2 + 2) for index in range(len(points) - 1)]
    obj = make_mesh_object(name, collection, vertices, faces)
    obj.data.materials.append(material)
    return obj


def create_curve_tube(
    name: str,
    points: Sequence[Sequence[float]],
    radius: float,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    cyclic: bool = False,
) -> bpy.types.Object:
    curve = bpy.data.curves.new(f"{name}_Curve", type="CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = radius
    curve.bevel_resolution = 3
    spline = curve.splines.new("NURBS" if len(points) > 3 else "POLY")
    spline.points.add(len(points) - 1)
    for point, coordinate in zip(spline.points, points):
        point.co = tuple(coordinate) + (1.0,)
    if spline.type == "NURBS":
        spline.order_u = min(3, len(points))
        spline.use_endpoint_u = not cyclic
        spline.use_cyclic_u = cyclic
    else:
        spline.use_cyclic_u = cyclic
    obj = bpy.data.objects.new(name, curve)
    collection.objects.link(obj)
    curve.materials.append(material)
    return obj


def sample_height(height: np.ndarray, x: float, y: float, size_x: float, size_y: float) -> float:
    rows, cols = height.shape
    col = np.clip((x / size_x + 0.5) * (cols - 1), 0.0, cols - 1.0)
    row = np.clip((y / size_y + 0.5) * (rows - 1), 0.0, rows - 1.0)
    col0 = int(math.floor(col))
    row0 = int(math.floor(row))
    col1 = min(col0 + 1, cols - 1)
    row1 = min(row0 + 1, rows - 1)
    fx = col - col0
    fy = row - row0
    top = height[row0, col0] * (1.0 - fx) + height[row0, col1] * fx
    bottom = height[row1, col0] * (1.0 - fx) + height[row1, col1] * fx
    return float(top * (1.0 - fy) + bottom * fy)


def look_at(obj: bpy.types.Object, target: Sequence[float]) -> None:
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()


def create_camera(
    name: str,
    scene: bpy.types.Scene,
    collection: bpy.types.Collection,
    location: Sequence[float],
    target: Sequence[float],
    lens: float = 45.0,
) -> bpy.types.Object:
    data = bpy.data.cameras.new(f"{name}_Data")
    data.lens = lens
    data.sensor_width = 36.0
    data.clip_start = 0.1
    data.clip_end = 5000.0
    camera = bpy.data.objects.new(name, data)
    camera.location = location
    camera["validation_camera"] = True
    collection.objects.link(camera)
    look_at(camera, target)
    if scene.camera is None:
        scene.camera = camera
    return camera


def create_sun(scene: bpy.types.Scene, collection: bpy.types.Collection, energy: float = 3.0, rotation=(0.55, -0.35, -0.8)) -> bpy.types.Object:
    data = bpy.data.lights.new(f"Sun_{scene.name}", type="SUN")
    data.energy = energy
    data.angle = math.radians(18.0)
    obj = bpy.data.objects.new(f"Sun_{scene.name}", data)
    obj.rotation_euler = rotation
    collection.objects.link(obj)
    return obj


def configure_scene(scene: bpy.types.Scene, background: Sequence[float], strength: float = 0.35) -> None:
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 2560
    scene.render.resolution_y = 1440
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.look = "AgX - Medium High Contrast"
    world = bpy.data.worlds.new(f"World_{scene.name}")
    world.use_nodes = True
    background_node = world.node_tree.nodes.get("Background")
    background_node.inputs["Color"].default_value = tuple(background[:3]) + (1.0,)
    background_node.inputs["Strength"].default_value = strength
    scene.world = world


def create_marker(name: str, collection: bpy.types.Collection, location: Sequence[float], property_name: str, property_value: str) -> bpy.types.Object:
    marker = bpy.data.objects.new(name, None)
    marker.empty_display_type = "SPHERE"
    marker.empty_display_size = 4.0
    marker.location = location
    marker[property_name] = property_value
    collection.objects.link(marker)
    return marker


def create_text(name: str, text: str, collection: bpy.types.Collection, location: Sequence[float], size: float, material: bpy.types.Material) -> bpy.types.Object:
    curve = bpy.data.curves.new(f"{name}_Text", type="FONT")
    curve.body = text
    curve.align_x = "CENTER"
    curve.align_y = "CENTER"
    curve.size = size
    curve.extrude = size * 0.015
    obj = bpy.data.objects.new(name, curve)
    obj.location = location
    collection.objects.link(obj)
    curve.materials.append(material)
    return obj


def create_scatter_points(
    name: str,
    points: Sequence[Sequence[float]],
    source_object: bpy.types.Object,
    collection: bpy.types.Collection,
    group_name: str,
    min_scale: float,
    max_scale: float,
) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(f"{name}_Points")
    mesh.from_pydata(points, [], [])
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)

    group = bpy.data.node_groups.get(group_name) or bpy.data.node_groups.new(group_name, "GeometryNodeTree")
    group.nodes.clear()
    group.interface.clear()
    group.interface.new_socket(name="Geometry", in_out="INPUT", socket_type="NodeSocketGeometry")
    group.interface.new_socket(name="Geometry", in_out="OUTPUT", socket_type="NodeSocketGeometry")
    nodes = group.nodes
    links = group.links
    group_input = nodes.new("NodeGroupInput")
    group_output = nodes.new("NodeGroupOutput")
    object_info = nodes.new("GeometryNodeObjectInfo")
    object_info.transform_space = "ORIGINAL"
    object_info.inputs["Object"].default_value = source_object
    if object_info.inputs.get("As Instance"):
        object_info.inputs["As Instance"].default_value = True
    instance = nodes.new("GeometryNodeInstanceOnPoints")
    random_rotation = nodes.new("FunctionNodeRandomValue")
    random_rotation.data_type = "FLOAT_VECTOR"
    random_rotation.inputs["Min"].default_value = (0.0, 0.0, 0.0)
    random_rotation.inputs["Max"].default_value = (0.0, 0.0, math.tau)
    random_scale = nodes.new("FunctionNodeRandomValue")
    random_scale.data_type = "FLOAT_VECTOR"
    random_scale.inputs["Min"].default_value = (min_scale, min_scale, min_scale)
    random_scale.inputs["Max"].default_value = (max_scale, max_scale, max_scale)
    links.new(group_input.outputs["Geometry"], instance.inputs["Points"])
    links.new(object_info.outputs["Geometry"], instance.inputs["Instance"])
    links.new(random_rotation.outputs["Value"], instance.inputs["Rotation"])
    links.new(random_scale.outputs["Value"], instance.inputs["Scale"])
    links.new(instance.outputs["Instances"], group_output.inputs["Geometry"])
    modifier = obj.modifiers.new(name=group_name, type="NODES")
    modifier.node_group = group
    obj["scatter_source"] = source_object.name
    obj["point_count"] = len(points)
    obj["min_scale"] = float(min_scale)
    obj["max_scale"] = float(max_scale)
    obj["distribution"] = "deterministic_masked_points"
    return obj


def save_float_exr(path: Path, name: str, data: np.ndarray) -> None:
    height, width = data.shape
    image = bpy.data.images.get(name) or bpy.data.images.new(name, width=width, height=height, float_buffer=True, alpha=True)
    rgba = np.empty((height, width, 4), dtype=np.float32)
    rgba[:, :, 0] = data
    rgba[:, :, 1] = data
    rgba[:, :, 2] = data
    rgba[:, :, 3] = 1.0
    image.pixels.foreach_set(rgba.ravel())
    image.filepath_raw = str(path)
    image.file_format = "OPEN_EXR"
    try:
        image.colorspace_settings.name = "Non-Color"
    except TypeError:
        pass
    image.save()
