"""Checks the item table and the item models in the editor, with no play needed:

- every item id in the C++ table is unique and has a name, a size and a stack;
- the JSON mirror the model generator reads is written and lists the same ids;
- every item with a coded model has a static mesh asset with all its material slots filled, no Nanite, and simple
  collision; items without a model are listed (they draw as a crate).

Run it with the editor closed:

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/item_table_test.py"

Results are in Saved/Logs/Riptide.log (search for RiptideItems).
"""
import json
import os

import unreal

import riptide_item_models

results = []


def log(msg):
    unreal.log("RiptideItems " + msg)


def check(passed, text):
    results.append(passed)
    log(("PASS " if passed else "FAIL ") + text)


ids = [str(i) for i in unreal.RiptideDataLibrary.item_ids()]
check(len(ids) >= 100, f"the item table has {len(ids)} items")
check(len(set(ids)) == len(ids), "every item id is unique")
for must in ("coconut", "rope", "stone_hatchet", "oar", "raft_kit", "canteen_clean", "fuel_drum", "survival_book"):
    check(unreal.RiptideDataLibrary.is_item(must), f"{must} is an item")

saved = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
tables = os.path.join(saved, "Generated", "riptide_tables.json")
ok = unreal.RiptideDataLibrary.export_tables(tables)
check(ok and os.path.exists(tables), "the tables export to JSON")
if ok:
    with open(tables, encoding="utf-8") as f:
        doc = json.load(f)
    exported = [item["id"] for item in doc["items"]]
    check(exported == ids, f"the JSON lists the same {len(exported)} items in the same order")
    check(doc["groups"]["wood"] == ["driftwood", "log"], "the wood group is driftwood then log")

assets = unreal.EditorAssetLibrary
modelled = [i for i in ids if i in riptide_item_models.MODELS]
missing = [i for i in ids if i not in riptide_item_models.MODELS]
log(f"{len(modelled)} items have models; {len(missing)} draw as a crate: {', '.join(missing)}")
bad = []
for item_id in modelled + ["crate"]:
    path = f"{riptide_item_models.ITEMS_PATH}/SM_Item_{item_id}"
    mesh = unreal.load_asset(path)
    if not mesh:
        bad.append(f"{item_id}: no mesh")
        continue
    for slot in mesh.get_editor_property("static_materials"):
        if not slot.get_editor_property("material_interface"):
            bad.append(f"{item_id}: slot {slot.get_editor_property('material_slot_name')} has no material")
    tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if tools.get_nanite_settings(mesh).get_editor_property("enabled"):
        bad.append(f"{item_id}: Nanite is on")
    if tools.get_simple_collision_count(mesh) == 0:
        bad.append(f"{item_id}: no simple collision")
    box = mesh.get_bounding_box()
    size = (box.max - box.min)
    if max(size.x, size.y, size.z) > 250.0 or max(size.x, size.y, size.z) < 1.0:
        bad.append(f"{item_id}: odd size {size.x:.0f} x {size.y:.0f} x {size.z:.0f} cm")
check(not bad, "every item model imported with materials, no Nanite and simple collision" + ("" if not bad else ": " + "; ".join(bad)))

# The inventory pictures need a running game: play the island map for a moment, take them all, and save a few.
state = {"ticks": 0, "handle": None, "done": False}
SAVE = ("coconut", "stone_hatchet", "oar", "rope", "canteen_clean", "lighter", "tarp")


def finish():
    log(f"RESULT {'PASS' if all(results) else 'FAIL'} ({len(results)} checks)")
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    state["ticks"] += 1
    try:
        # The editor is left to settle after the imports before the map is swapped (swapping at once crashed it).
        if state["ticks"] == 240:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Riptide/Maps/Island_Test")
        if state["ticks"] == 420:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world or state["done"]:
            if state["ticks"] > 3000 and not state["done"]:
                check(False, "the game never started for the icon check")
                finish()
            return
        if unreal.GameplayStatics.get_time_seconds(world) < 2.0:
            return
        state["done"] = True
        taken = unreal.RiptideDataLibrary.take_all_icons(world)
        check(taken == len(ids), f"inventory pictures taken for {taken} of {len(ids)} items")
        out = os.path.join(saved, "Screenshots", "WindowsEditor")
        os.makedirs(out, exist_ok=True)
        for item_id in SAVE:
            unreal.RiptideDataLibrary.save_icon(world, item_id, os.path.join(out, f"icon_{item_id}.png"))
        finish()
    except Exception as err:  # noqa: BLE001 - report and stop
        import traceback
        log("error " + traceback.format_exc())
        check(False, f"the icon check failed: {err}")
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
