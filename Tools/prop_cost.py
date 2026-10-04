"""Lists what the island map draws outside Nanite: every prop batch with its mesh, triangles, instances, Nanite,
shadows and material blend modes, so the slow passes can be traced to the meshes behind them. Editor only:

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/prop_cost.py"
"""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
levels.load_level("/Game/Riptide/Maps/Island_Test")
tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
rows = []
for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
    for comp in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = comp.get_editor_property("static_mesh")
        if not mesh:
            continue
        instances = comp.get_instance_count() if isinstance(comp, unreal.InstancedStaticMeshComponent) else 1
        nanite = tools.get_nanite_settings(mesh).get_editor_property("enabled")
        tris = mesh.get_num_triangles(0)
        blends = set()
        for i in range(mesh.get_num_sections(0)):
            mat = comp.get_material(i)
            if mat:
                base = mat.get_base_material() if hasattr(mat, "get_base_material") else mat
                blends.add(str(base.get_editor_property("blend_mode")).split(".")[-1])
        rows.append((instances * tris, mesh.get_name(), instances, tris, nanite, comp.get_editor_property("cast_shadow"), ",".join(sorted(blends)), actor.get_actor_label()))
rows.sort(reverse=True)
unreal.log("RiptideCost mesh | instances | triangles each | Nanite | shadow | blend | actor")
for total, name, instances, tris, nanite, shadow, blends, label in rows[:60]:
    unreal.log(f"RiptideCost {name} | {instances} | {tris} | {'nanite' if nanite else 'PLAIN'} | {'shadow' if shadow else 'noshadow'} | {blends} | {label}")
plain = sum(r[0] for r in rows if not r[4])
heavy_plain = [r[1] for r in rows if not r[4] and r[3] > 50000]
unreal.log(f"RiptideCost RESULT {'PASS' if not heavy_plain else 'FAIL'} {len(rows)} batches; non-Nanite triangles if all drawn: {plain}"
           + (f"; heavy meshes not Nanite: {', '.join(heavy_plain)}" if heavy_plain else ""))
unreal.SystemLibrary.quit_editor()
