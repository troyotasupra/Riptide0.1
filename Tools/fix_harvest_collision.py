"""Gives the already-imported harvestable plants their look-trace box (riptide_island_props.harvest_collision) without
re-importing every model. Run once after pulling; new imports get it from _dress.

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/fix_harvest_collision.py"
"""
import unreal

import riptide_island_props

state = {"ticks": 0, "handle": None}


def tick(dt):
    state["ticks"] += 1
    if state["ticks"] != 20:
        return
    try:
        catalogue = riptide_island_props.model_catalogue()
        for model in riptide_island_props.HARVESTED_PLANTS:
            for entry in catalogue.get(model, []):
                mesh = unreal.load_asset(entry["path"])
                ok = riptide_island_props.harvest_collision(mesh, entry["path"]) if mesh else False
                unreal.log(f"RiptideFix {entry['path'].split('/')[-1]}: {'boxed' if ok else 'FAILED'}")
        unreal.log("RiptideFix RESULT done")
    except Exception:  # noqa: BLE001
        import traceback
        unreal.log("RiptideFix error " + traceback.format_exc())
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.SystemLibrary.quit_editor()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
