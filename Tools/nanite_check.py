"""Makes sure every heavy island model is Nanite, building it if it isn't, and reports each one. Editor only:

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/nanite_check.py"
"""
import unreal

import riptide_island_props

tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
assets = unreal.EditorAssetLibrary
fixed, bad = [], []
for model, entries in riptide_island_props.model_catalogue().items():
    for entry in entries:
        mesh = unreal.load_asset(entry["path"])
        if not mesh:
            bad.append(f"{entry['path']}: missing")
            continue
        tris = mesh.get_num_triangles(0)
        settings = tools.get_nanite_settings(mesh)
        want = tris > 5000 or entry.get("kind") == "rock"
        enabled = settings.get_editor_property("enabled")
        unreal.log(f"RiptideNanite {mesh.get_name()}: {tris} triangles, nanite {'on' if enabled else 'OFF'}, "
                   f"keep {settings.get_editor_property('keep_percent_triangles'):.2f}")
        if want and not enabled:
            settings.set_editor_property("enabled", True)
            settings.set_editor_property("keep_percent_triangles", 0.2 if tris > 500000 else 0.5 if tris > 100000 else 1.0)
            ok = tools.set_nanite_settings(mesh, settings, True)
            again = tools.get_nanite_settings(mesh).get_editor_property("enabled")
            unreal.log(f"RiptideNanite   set on {mesh.get_name()}: returned {ok}, now {'on' if again else 'STILL OFF'}")
            assets.save_asset(entry["path"], only_if_is_dirty=False)
            if again:
                fixed.append(mesh.get_name())
            else:
                bad.append(f"{mesh.get_name()}: Nanite would not turn on")
unreal.log(f"RiptideNanite RESULT fixed {len(fixed)} ({', '.join(fixed)}); problems {len(bad)} ({'; '.join(bad)})")
unreal.SystemLibrary.quit_editor()
