#
# Copyright (c) 2026, Daily
#
# SPDX-License-Identifier: BSD-2-Clause
#

"""Creates the demo's level, /Game/Maps/PipecatLevel, if it isn't there.

The game builds the world (the ground, the sky, the plants and the water) when
play begins, so the level only has where the player starts. Add your own
actors to it in the editor: give them the OnGround tag, and they stay on the
ground as the terrain changes.

Run it in the editor, e.g.:

    UnrealEditor-Cmd PipecatDemo.uproject -run=pythonscript \\
        -script=Scripts/create_level.py -EnablePlugins=PythonScriptPlugin
"""

import unreal

LEVEL = "/Game/Maps/PipecatLevel"

if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
    unreal.log(f"{LEVEL} is already there")
else:
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(LEVEL):
        raise RuntimeError(f"Unable to create {LEVEL}")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 120.0))
    if not levels.save_current_level():
        raise RuntimeError(f"Unable to save {LEVEL}")
    unreal.log(f"Created {LEVEL}")
