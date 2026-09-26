import bpy
import os
import math
import mathutils

def log(msg):
    print(f'[CONVERT] {msg}')

# 1. Blend file is already loaded via command-line argument or open if needed
blend_path = '/home/https/Área de trabalho/workspace/rpg/modelos 3d animados/novos/fbx file/blender file.blend'
if not bpy.data.objects.get('blender file_Rigify'):
    log(f'Loading {blend_path}...')
    bpy.ops.wm.open_mainfile(filepath=blend_path)
else:
    log('Blend file already loaded.')

# Delete cameras, lights, colliders, WGT objects
for obj in list(bpy.data.objects):
    if obj.type in ('CAMERA', 'LIGHT') or obj.name.startswith('WGT-') or 'Collider' in obj.name:
        bpy.data.objects.remove(obj, do_unlink=True)

rig = bpy.data.objects.get('blender file_Rigify')
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode='POSE')

# Move hands into horizontal T-pose
log('Setting T-pose on rig...')
rig.pose.bones['hand_ik.L'].location.z += 0.2412
rig.pose.bones['hand_ik.R'].location.z += 0.2412
bpy.context.view_layer.update()

# Extract joint positions from Rigify DEF bones in T-pose
def joint_pos(bname):
    pb = rig.pose.bones.get(bname)
    if not pb:
        log(f'WARNING: Bone {bname} not found!')
        return mathutils.Vector((0, 0, 0))
    return rig.matrix_world @ pb.head

joints = {
    'Hips': joint_pos('DEF-pelvis'),
    'Spine': joint_pos('DEF-spine.001'),
    'Spine1': joint_pos('DEF-spine.002'),
    'Spine2': joint_pos('DEF-spine.003'),
    'Neck': joint_pos('DEF-spine.004'),
    'Head': joint_pos('DEF-spine.006'),
    'LeftShoulder': joint_pos('DEF-shoulder.L'),
    'LeftArm': joint_pos('DEF-upper_arm.L'),
    'LeftForeArm': joint_pos('DEF-forearm.L'),
    'LeftHand': joint_pos('DEF-hand.L'),
    'LeftHandMiddle1': joint_pos('DEF-f_middle.01.L'),
    'RightShoulder': joint_pos('DEF-shoulder.R'),
    'RightArm': joint_pos('DEF-upper_arm.R'),
    'RightForeArm': joint_pos('DEF-forearm.R'),
    'RightHand': joint_pos('DEF-hand.R'),
    'RightHandMiddle1': joint_pos('DEF-f_middle.01.R'),
    'LeftUpLeg': joint_pos('DEF-thigh.L'),
    'LeftLeg': joint_pos('DEF-shin.L'),
    'LeftFoot': joint_pos('DEF-foot.L'),
    'LeftToeBase': joint_pos('DEF-toe.L'),
    'RightUpLeg': joint_pos('DEF-thigh.R'),
    'RightLeg': joint_pos('DEF-shin.R'),
    'RightFoot': joint_pos('DEF-foot.R'),
    'RightToeBase': joint_pos('DEF-toe.R'),
}

left_arm = joints['LeftArm']
left_hand = joints['LeftHand']
log(f'LeftArm joint: {left_arm}')
log(f'LeftHand joint: {left_hand}')

# Apply the Armature modifier on all meshes to freeze them in T-pose
char_mesh_names = ['Body', 'Bra', 'Underwear_Bottoms', 'LongCurly', 'Eye', 'Teeth', 'Tongue']
meshes = [bpy.data.objects.get(name) for name in char_mesh_names if bpy.data.objects.get(name)]

bpy.ops.object.mode_set(mode='OBJECT')
for mesh in meshes:
    bpy.context.view_layer.objects.active = mesh
    if mesh.data.shape_keys:
        log(f'Clearing shape keys on {mesh.name}')
        mesh.shape_key_clear()
    for mod in list(mesh.modifiers):
        if mod.type == 'ARMATURE':
            log(f'Applying Armature modifier on {mesh.name}')
            bpy.ops.object.modifier_apply(modifier=mod.name)

# Define DEF to Mixamo bone mapping
DEF_TO_MIXAMO = {}
# Head & Face
for prefix in ['DEF-spine.006', 'DEF-brow', 'DEF-cheek', 'DEF-chin', 'DEF-ear', 'DEF-eye.', 'DEF-forehead', 
               'DEF-jaw', 'DEF-lid', 'DEF-lip', 'DEF-nose', 'DEF-teeth', 'DEF-temple', 'DEF-tongue']:
    DEF_TO_MIXAMO[prefix] = 'mixamorig:Head'
# Neck
DEF_TO_MIXAMO['DEF-spine.004'] = 'mixamorig:Neck'
DEF_TO_MIXAMO['DEF-spine.005'] = 'mixamorig:Neck'
# Spine & Hips
DEF_TO_MIXAMO['DEF-spine.003'] = 'mixamorig:Spine2'
DEF_TO_MIXAMO['DEF-spine.002'] = 'mixamorig:Spine1'
DEF_TO_MIXAMO['DEF-spine.001'] = 'mixamorig:Spine'
DEF_TO_MIXAMO['DEF-spine'] = 'mixamorig:Hips'
DEF_TO_MIXAMO['DEF-pelvis'] = 'mixamorig:Hips'
# Left Arm & Shoulder
DEF_TO_MIXAMO['DEF-shoulder.L'] = 'mixamorig:LeftShoulder'
DEF_TO_MIXAMO['DEF-upper_arm.L'] = 'mixamorig:LeftArm'
DEF_TO_MIXAMO['DEF-upper_arm.L.001'] = 'mixamorig:LeftArm'
DEF_TO_MIXAMO['DEF-breast.L'] = 'mixamorig:LeftArm'
DEF_TO_MIXAMO['DEF-breast_twist.L'] = 'mixamorig:LeftArm'
DEF_TO_MIXAMO['DEF-forearm.L'] = 'mixamorig:LeftForeArm'
DEF_TO_MIXAMO['DEF-forearm.L.001'] = 'mixamorig:LeftForeArm'
DEF_TO_MIXAMO['DEF-elbow_share.L'] = 'mixamorig:LeftForeArm'
DEF_TO_MIXAMO['DEF-hand.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-palm.01.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-palm.02.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-palm.03.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-palm.04.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-thumb.01.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-thumb.02.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-thumb.03.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_index.01.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_index.02.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_index.03.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_ring.01.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_ring.02.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_ring.03.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_pinky.01.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_pinky.02.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_pinky.03.L'] = 'mixamorig:LeftHand'
DEF_TO_MIXAMO['DEF-f_middle.01.L'] = 'mixamorig:LeftHandMiddle1'
DEF_TO_MIXAMO['DEF-f_middle.02.L'] = 'mixamorig:LeftHandMiddle1'
DEF_TO_MIXAMO['DEF-f_middle.03.L'] = 'mixamorig:LeftHandMiddle1'
# Right Arm & Shoulder
DEF_TO_MIXAMO['DEF-shoulder.R'] = 'mixamorig:RightShoulder'
DEF_TO_MIXAMO['DEF-upper_arm.R'] = 'mixamorig:RightArm'
DEF_TO_MIXAMO['DEF-upper_arm.R.001'] = 'mixamorig:RightArm'
DEF_TO_MIXAMO['DEF-breast.R'] = 'mixamorig:RightArm'
DEF_TO_MIXAMO['DEF-breast_twist.R'] = 'mixamorig:RightArm'
DEF_TO_MIXAMO['DEF-forearm.R'] = 'mixamorig:RightForeArm'
DEF_TO_MIXAMO['DEF-forearm.R.001'] = 'mixamorig:RightForeArm'
DEF_TO_MIXAMO['DEF-elbow_share.R'] = 'mixamorig:RightForeArm'
DEF_TO_MIXAMO['DEF-hand.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-palm.01.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-palm.02.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-palm.03.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-palm.04.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-thumb.01.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-thumb.02.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-thumb.03.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_index.01.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_index.02.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_index.03.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_ring.01.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_ring.02.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_ring.03.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_pinky.01.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_pinky.02.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_pinky.03.R'] = 'mixamorig:RightHand'
DEF_TO_MIXAMO['DEF-f_middle.01.R'] = 'mixamorig:RightHandMiddle1'
DEF_TO_MIXAMO['DEF-f_middle.02.R'] = 'mixamorig:RightHandMiddle1'
DEF_TO_MIXAMO['DEF-f_middle.03.R'] = 'mixamorig:RightHandMiddle1'
# Left Leg
DEF_TO_MIXAMO['DEF-thigh.L'] = 'mixamorig:LeftUpLeg'
DEF_TO_MIXAMO['DEF-thigh.L.001'] = 'mixamorig:LeftUpLeg'
DEF_TO_MIXAMO['DEF-shin.L'] = 'mixamorig:LeftLeg'
DEF_TO_MIXAMO['DEF-shin.L.001'] = 'mixamorig:LeftLeg'
DEF_TO_MIXAMO['DEF-knee_share.L'] = 'mixamorig:LeftLeg'
DEF_TO_MIXAMO['DEF-foot.L'] = 'mixamorig:LeftFoot'
DEF_TO_MIXAMO['DEF-toe.L'] = 'mixamorig:LeftToeBase'
DEF_TO_MIXAMO['DEF-toe_big.L'] = 'mixamorig:LeftToeBase'
DEF_TO_MIXAMO['DEF-toe_index.L'] = 'mixamorig:LeftToeBase'
DEF_TO_MIXAMO['DEF-toe_mid.L'] = 'mixamorig:LeftToeBase'
DEF_TO_MIXAMO['DEF-toe_ring.L'] = 'mixamorig:LeftToeBase'
DEF_TO_MIXAMO['DEF-toe_pinky.L'] = 'mixamorig:LeftToeBase'
# Right Leg
DEF_TO_MIXAMO['DEF-thigh.R'] = 'mixamorig:RightUpLeg'
DEF_TO_MIXAMO['DEF-thigh.R.001'] = 'mixamorig:RightUpLeg'
DEF_TO_MIXAMO['DEF-shin.R'] = 'mixamorig:RightLeg'
DEF_TO_MIXAMO['DEF-shin.R.001'] = 'mixamorig:RightLeg'
DEF_TO_MIXAMO['DEF-knee_share.R'] = 'mixamorig:RightLeg'
DEF_TO_MIXAMO['DEF-foot.R'] = 'mixamorig:RightFoot'
DEF_TO_MIXAMO['DEF-toe.R'] = 'mixamorig:RightToeBase'
DEF_TO_MIXAMO['DEF-toe_big.R'] = 'mixamorig:RightToeBase'
DEF_TO_MIXAMO['DEF-toe_index.R'] = 'mixamorig:RightToeBase'
DEF_TO_MIXAMO['DEF-toe_mid.R'] = 'mixamorig:RightToeBase'
DEF_TO_MIXAMO['DEF-toe_ring.R'] = 'mixamorig:RightToeBase'
DEF_TO_MIXAMO['DEF-toe_pinky.R'] = 'mixamorig:RightToeBase'

def get_target_group(def_name):
    if def_name in DEF_TO_MIXAMO:
        return DEF_TO_MIXAMO[def_name]
    for k, v in DEF_TO_MIXAMO.items():
        if def_name.startswith(k):
            return v
    return None

# Remap vertex groups on all meshes
log('Remapping vertex groups on meshes...')
for mesh in meshes:
    # Build map of vertex weights per target group
    num_verts = len(mesh.data.vertices)
    weights = {} # target_name -> [float * num_verts]
    
    # Collect weights from existing vertex groups
    vg_map = {vg.index: vg.name for vg in mesh.vertex_groups}
    for v in mesh.data.vertices:
        for g in v.groups:
            orig_name = vg_map.get(g.group)
            if not orig_name: continue
            target = get_target_group(orig_name)
            if not target: continue
            if target not in weights:
                weights[target] = [0.0] * num_verts
            weights[target][v.index] += g.weight
            
    # Clear all existing vertex groups
    mesh.vertex_groups.clear()
    
    # Create new Mixamo vertex groups and assign weights
    for target_name, w_list in weights.items():
        new_vg = mesh.vertex_groups.new(name=target_name)
        for v_idx, w in enumerate(w_list):
            if w > 0.0001:
                new_vg.add([v_idx], min(1.0, w), 'REPLACE')
    log(f'Mesh {mesh.name}: {len(mesh.vertex_groups)} Mixamo vertex groups created.')

# Delete old Rigify armature
bpy.data.objects.remove(rig, do_unlink=True)

# Now, import Eve's Mixamo armature
log('Importing Eve Mixamo armature...')
from io_scene_fbx import import_fbx
class DummyOp:
    use_custom_normals = True
    use_subsurf = False
    use_custom_props = True
    use_custom_props_enum_as_string = True
    ignore_leaf_bones = False
    force_connect_children = False
    automatic_bone_orientation = False
    primary_bone_axis = 'Y'
    secondary_bone_axis = 'X'
    use_prepost_rot = True
    axis_forward = '-Z'
    axis_up = 'Y'
    global_scale = 1.0
    bake_space_transform = False
    use_image_search = True
    colors_type = 'SRGB'
    def report(self, type, msg):
        pass

import_fbx.load(DummyOp(), bpy.context, filepath='/home/https/Área de trabalho/workspace/rpg/modelos 3d animados/Eve By J.Gonzales.fbx')

# Delete Eve mesh
for o in list(bpy.data.objects):
    if o.type == 'MESH' and o.name not in char_mesh_names:
        bpy.data.objects.remove(o, do_unlink=True)

new_arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
new_arm.name = 'MixamoArmature'

# Apply scale on imported armature (FBX comes in cm scale 0.01)
bpy.context.view_layer.objects.active = new_arm
bpy.ops.object.transform_apply(scale=True)

# Align edit bones to character joints
bpy.ops.object.mode_set(mode='EDIT')
ebones = new_arm.data.edit_bones

# Mixamo bones in Eve: 'mixamorig:Hips', 'mixamorig:Spine', etc.
bone_keys = {
    'mixamorig:Hips': ('Hips', 'Spine'),
    'mixamorig:Spine': ('Spine', 'Spine1'),
    'mixamorig:Spine1': ('Spine1', 'Spine2'),
    'mixamorig:Spine2': ('Spine2', 'Neck'),
    'mixamorig:Neck': ('Neck', 'Head'),
    'mixamorig:LeftShoulder': ('LeftShoulder', 'LeftArm'),
    'mixamorig:LeftArm': ('LeftArm', 'LeftForeArm'),
    'mixamorig:LeftForeArm': ('LeftForeArm', 'LeftHand'),
    'mixamorig:LeftHand': ('LeftHand', 'LeftHandMiddle1'),
    'mixamorig:RightShoulder': ('RightShoulder', 'RightArm'),
    'mixamorig:RightArm': ('RightArm', 'RightForeArm'),
    'mixamorig:RightForeArm': ('RightForeArm', 'RightHand'),
    'mixamorig:RightHand': ('RightHand', 'RightHandMiddle1'),
    'mixamorig:LeftUpLeg': ('LeftUpLeg', 'LeftLeg'),
    'mixamorig:LeftLeg': ('LeftLeg', 'LeftFoot'),
    'mixamorig:LeftFoot': ('LeftFoot', 'LeftToeBase'),
    'mixamorig:RightUpLeg': ('RightUpLeg', 'RightLeg'),
    'mixamorig:RightLeg': ('RightLeg', 'RightFoot'),
    'mixamorig:RightFoot': ('RightFoot', 'RightToeBase'),
}

for bname, (head_k, tail_k) in bone_keys.items():
    eb = ebones.get(bname)
    if eb and head_k in joints and tail_k in joints:
        eb.head = joints[head_k]
        eb.tail = joints[tail_k]

# For head bone: tail extends up along Z
eb_head = ebones.get('mixamorig:Head')
if eb_head and 'Head' in joints:
    eb_head.head = joints['Head']
    eb_head.tail = joints['Head'] + mathutils.Vector((0, 0, 0.15))

# For toe bones: tail extends forward along -Y or +Z
for bname in ['mixamorig:LeftToeBase', 'mixamorig:RightToeBase']:
    eb = ebones.get(bname)
    if eb:
        k = 'LeftToeBase' if 'Left' in bname else 'RightToeBase'
        eb.head = joints[k]
        eb.tail = joints[k] + mathutils.Vector((0, -0.08, 0))

bpy.ops.object.mode_set(mode='OBJECT')

# Add Armature modifier on all meshes pointing to new_arm
for mesh in meshes:
    bpy.context.view_layer.objects.active = mesh
    mesh.parent = new_arm
    mod = mesh.modifiers.new('Armature', 'ARMATURE')
    mod.object = new_arm

# Material setup: ensure direct connections from image textures to Principled BSDF
log('Cleaning up materials to standard PBR...')
for mat in bpy.data.materials:
    if not mat.use_nodes or not mat.node_tree:
        continue
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    
    # Find images
    diff_img, norm_img, rough_img, opac_img = None, None, None, None
    for n in nodes:
        if n.type == 'TEX_IMAGE' and n.image:
            name = n.image.name.lower()
            if 'diffuse' in name or 'basecolor' in name or 'albedo' in name or 'color' in name:
                diff_img = n.image
            elif 'normal' in name:
                norm_img = n.image
            elif 'rough' in name:
                rough_img = n.image
            elif 'opacity' in name or 'alpha' in name:
                opac_img = n.image
                
    bsdf = [n for n in nodes if n.type == 'BSDF_PRINCIPLED']
    if not bsdf:
        continue
    b = bsdf[0]
    
    # Clear existing input links to Base Color, Normal, Alpha, Roughness, Metallic
    for inp_name in ['Base Color', 'Normal', 'Alpha', 'Roughness', 'Metallic']:
        if inp_name in b.inputs:
            for link in list(b.inputs[inp_name].links):
                links.remove(link)
                
    b.inputs['Roughness'].default_value = 0.72
    b.inputs['Metallic'].default_value = 0.0
    
    if diff_img:
        tex_node = nodes.new('ShaderNodeTexImage')
        tex_node.image = diff_img
        links.new(tex_node.outputs['Color'], b.inputs['Base Color'])
        
    if norm_img:
        tex_norm = nodes.new('ShaderNodeTexImage')
        tex_norm.image = norm_img
        tex_norm.image.colorspace_settings.name = 'Non-Color'
        norm_map = nodes.new('ShaderNodeNormalMap')
        links.new(tex_norm.outputs['Color'], norm_map.inputs['Color'])
        links.new(norm_map.outputs['Normal'], b.inputs['Normal'])
        
    if opac_img:
        tex_opac = nodes.new('ShaderNodeTexImage')
        tex_opac.image = opac_img
        tex_opac.image.colorspace_settings.name = 'Non-Color'
        links.new(tex_opac.outputs['Color'], b.inputs['Alpha'])
        if hasattr(mat, 'blend_method'):
            mat.blend_method = 'HASHED'

# Export GLB
out_glb = '/home/https/Área de trabalho/workspace/rpg/demo/assets/char_paladina_raw.glb'
log(f'Exporting GLB to {out_glb}...')
# Select new_arm and meshes
bpy.ops.object.select_all(action='DESELECT')
new_arm.select_set(True)
for mesh in meshes:
    mesh.select_set(True)
bpy.context.view_layer.objects.active = new_arm

bpy.ops.export_scene.gltf(
    filepath=out_glb,
    export_format='GLB',
    use_selection=True,
    export_skins=True,
    export_animations=False,
    export_apply=False,
    export_materials='EXPORT',
    export_image_format='AUTO',
    export_attributes=True
)
log(f'Export finished! File size: {os.path.getsize(out_glb)} bytes')
