# Etapa do Blender em `build-extra-assets.mjs`: abre a fonte, junta as malhas, reduz e exporta GLB com
# as texturas embutidas.
#
#   blender -b --factory-startup [arquivo.blend] --python extra-assets-blender.py -- <entrada> <objeto|*> <tris> <saida.glb>
#
#   entrada   o .blend já aberto (use "-") ou um .glb/.gltf para importar
#   objeto    nome do objeto a manter, ou "*" para todos os objetos de malha
#   tris      meta de triângulos; 0 mantém a malha como está. A redução é o Decimate por colapso,
#             que respeita as costuras de UV das malhas fragmentadas geradas por IA
#
# Só lê a fonte (nunca salva o .blend). Escala, eixo e texturas finais ficam com o script Node.
import bpy, sys

argv = sys.argv[sys.argv.index('--') + 1:]
src, name, tris, out = argv[0], argv[1], int(argv[2]), argv[3]
if src != '-':
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)

keep = [o for o in bpy.data.objects if o.type == 'MESH' and (name == '*' or o.name == name)]
if not keep:
    print(f'#### ERRO objeto {name!r} não encontrado', file=sys.stderr)
    sys.exit(2)
for o in list(bpy.data.objects):
    if o not in keep and o.type == 'MESH':
        bpy.data.objects.remove(o, do_unlink=True)
bpy.context.view_layer.update()
bpy.ops.object.select_all(action='DESELECT')
for o in keep:
    o.hide_set(False); o.hide_render = False; o.select_set(True)
bpy.context.view_layer.objects.active = keep[0]
if len(keep) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
# a transformação dos pais (o nó raiz do GLB) entra na malha antes de reduzir
bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

before = sum(len(p.vertices) - 2 for p in obj.data.polygons)
if tris and before > tris:
    # malhas de IA vêm com vértices duplicados em cada costura; fundidos, o colapso deixa de tratá-las
    # como borda. A UV fica guardada por canto de face e não se perde.
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=0.0005)
    bpy.ops.object.mode_set(mode='OBJECT')
    mod = obj.modifiers.new('reduz', 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = tris / before
    mod.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=mod.name)
after = sum(len(p.vertices) - 2 for p in obj.data.polygons)

bpy.ops.export_scene.gltf(
    filepath=out, export_format='GLB', use_selection=True, export_apply=True,
    export_image_format='AUTO', export_materials='EXPORT', export_animations=False, export_yup=True,
)
print(f'#### exportado {obj.name}: {before} → {after} triângulos')
