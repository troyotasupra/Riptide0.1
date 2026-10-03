"""Checks crafting: a crew member twists fiber into rope, can't make a hatchet short of flint, makes one with it,
and learns the campfire from the survival book; eating a coconut uses it up.

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound
        -ExecCmds="py <project>/Tools/craft_test.py"

Results: RiptideCraft lines in Saved/Logs/Riptide.log.
"""
import unreal

state = {"ticks": 0, "handle": None, "phase": "open", "since": 0.0, "results": []}


def log(msg):
    unreal.log("RiptideCraft " + msg)


def check(name, ok, detail):
    state["results"].append(ok)
    log(("PASS " if ok else "FAIL ") + name + " (" + detail + ")")


def finish():
    log("RESULT %s (%d checks)" % ("PASS" if all(state["results"]) else "FAIL", len(state["results"])))
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def count(walker, item_id):
    return unreal.RiptideDataLibrary.count_carried(walker, item_id)


def enter(phase, t):
    state["phase"] = phase
    state["since"] = t


def tick(dt):
    state["ticks"] += 1
    try:
        if state["ticks"] == 30:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Riptide/Maps/Island_Test")
        if state["ticks"] == 150:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:
            if state["ticks"] > 4000:
                check("the game started", False, "never")
                finish()
            return
        t = unreal.GameplayStatics.get_time_seconds(world)
        walkers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCharacter)
        if not walkers or t < 2.0:
            return
        walker = walkers[0]
        crafting = walker.get_crafting()
        since = t - state["since"]
        phase = state["phase"]
        if phase == "open":
            known = [str(k) for k in crafting.get_known_recipes()]
            check("a castaway knows rope, the hatchet, the oar and the raft", set(known) == {"rope", "stone_hatchet", "oar", "raft_kit"}, ", ".join(known))
            check("rope can't be made with empty pockets", not crafting.can_make("rope"), "can_make %s" % crafting.can_make("rope"))
            walker.give_item("fiber", 5)
            check("rope can be made with five fiber", crafting.can_make("rope"), "fiber %d" % count(walker, "fiber"))
            crafting.craft("rope")
            check("making rope takes a moment", crafting.is_crafting(), "crafting %s" % crafting.get_crafting())
            enter("rope", t)
        elif phase == "rope" and since > 3.0:
            check("the fiber became rope", count(walker, "rope") == 1 and count(walker, "fiber") == 0, "rope %d, fiber %d" % (count(walker, "rope"), count(walker, "fiber")))
            walker.give_item("driftwood", 1)
            check("a hatchet needs flint too", not crafting.can_make("stone_hatchet"), "can_make %s" % crafting.can_make("stone_hatchet"))
            walker.give_item("flint", 2)
            check("with wood, flint and rope the hatchet can be made", crafting.can_make("stone_hatchet"), "")
            crafting.craft("stone_hatchet")
            enter("hatchet", t)
        elif phase == "hatchet" and since > 5.0:
            check("the hatchet is made and the makings spent", count(walker, "stone_hatchet") == 1 and count(walker, "rope") == 0 and count(walker, "flint") == 0,
                  "hatchet %d, rope %d, flint %d, driftwood %d" % (count(walker, "stone_hatchet"), count(walker, "rope"), count(walker, "flint"), count(walker, "driftwood")))
            check("two things were made", crafting.get_made_count() == 2, "%d" % crafting.get_made_count())
            walker.give_item("survival_book", 1)
            uid = unreal.RiptideDataLibrary.first_carried_uid(walker, "survival_book")
            walker.use_item(0, uid) if uid else None
            # (the book may have landed in the pack: try both grids)
            if uid and not crafting.knows("campfire_kit"):
                walker.use_item(1, uid)
            enter("book", t)
        elif phase == "book" and since > 0.5:
            check("reading the survival book teaches the campfire and more", crafting.knows("campfire_kit") and crafting.knows("spear") and crafting.knows("torch"),
                  ", ".join(str(k) for k in crafting.get_known_recipes()))
            walker.give_item("coconut", 2)
            walker.get_survival().set_vitals(40.0, 30.0, 30.0)
            state["vitals"] = (walker.get_survival().get_hunger(), walker.get_survival().get_thirst())
            uid = unreal.RiptideDataLibrary.first_carried_uid(walker, "coconut")
            walker.use_item(0, uid)
            if count(walker, "coconut") == 2:
                walker.use_item(1, uid)
            enter("ate", t)
        elif phase == "ate" and since > 0.5:
            check("eating a coconut uses one up", count(walker, "coconut") == 1, "coconuts %d" % count(walker, "coconut"))
            survival = walker.get_survival()
            check("the coconut fed and watered them", survival.get_hunger() > state["vitals"][0] + 3 and survival.get_thirst() > state["vitals"][1] + 3,
                  "hunger %.0f -> %.0f, thirst %.0f -> %.0f" % (state["vitals"][0], survival.get_hunger(), state["vitals"][1], survival.get_thirst()))
            walker.give_item("bandage", 1)
            walker.use_item(0, unreal.RiptideDataLibrary.first_carried_uid(walker, "bandage"))
            enter("bandaged", t)
        elif phase == "bandaged" and since > 0.5:
            check("a bandage heals and is used up", walker.get_survival().get_health() > 55.0 and count(walker, "bandage") == 0,
                  "health %.0f, bandages %d" % (walker.get_survival().get_health(), count(walker, "bandage")))
            finish()
    except Exception as err:  # noqa: BLE001
        import traceback
        log("error " + traceback.format_exc())
        check("the run completed", False, str(err))
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
