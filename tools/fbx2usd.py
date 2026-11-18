# blender -b -P tools/fbx2usd.py -- in.fbx out.usda   (import an FBX, export OpenUSD)
import bpy, sys
src, dst = sys.argv[sys.argv.index('--')+1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
objs=list(bpy.context.scene.objects)
print("IMPORTED", len(objs), "objects; meshes", sum(o.type=='MESH' for o in objs), "armatures", sum(o.type=='ARMATURE' for o in objs))
bpy.ops.wm.usd_export(filepath=dst, export_materials=True, export_textures_mode='NEW', overwrite_textures=True, generate_preview_surface=True, export_armatures=True, export_animation=False, relative_paths=True)
print("EXPORTED", dst)
