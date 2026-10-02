#
# Copyright (c) 2026, Daily
#
# SPDX-License-Identifier: BSD-2-Clause
#

"""Creates the demo's materials, in /Game/Pipecat.

- MI_Ground, the ground's: the Third Person template's gray prototype grid,
  with dirt on top of its color. Patches of it, which the game sets with the
  Patchiness parameter, and mud where the ground's vertex color is red, e.g.
  on the shores. With Detail, the game also makes the color drier in places,
  and grainy, as grass comes.
- M_Glow, a glow around a mesh's edges, for the player while they speak. The
  game sets its Color, and how much it glows with Glow, which looks the same
  however bright the scene is.

setup.sh runs it in the editor, e.g.:

    UnrealEditor-Cmd PipecatDemo.uproject -run=pythonscript \\
        -script=Scripts/create_materials.py -EnablePlugins=PythonScriptPlugin
"""

import unreal

GRID = "/Game/LevelPrototyping/Materials/M_PrototypeGrid"
GRAY_GRID = "/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray"
GROUND = "/Game/Pipecat/M_Ground"
GROUND_INSTANCE = "/Game/Pipecat/MI_Ground"
GLOW = "/Game/Pipecat/M_Glow"

# How dirt, with ragged edges, covers the grid's color, and how the color
# varies.
GROUND_CODE = """
float Dirt = saturate((Patches + 0.3 * (Grain - 0.5) - (1.0 - Patchiness)) * 5.0);
Dirt = max(Dirt, saturate(Mud * (0.4 + Mottle)));
float3 Dry = Grid * float3(1.5, 1.15, 0.7);
float3 Color = lerp(Grid, Dry, saturate(Mottle * 2.0 - 0.6));
Color = lerp(Color, DirtColor, 0.8 * Dirt);
return lerp(Grid, Color * (0.8 + 0.4 * Grain), Detail);
"""

assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
editing = unreal.MaterialEditingLibrary

for path in [GROUND_INSTANCE, GROUND, GLOW]:
    if assets.does_asset_exist(path):
        assets.delete_asset(path)

#
# The ground
#

material = assets.duplicate_asset(GRID, GROUND)

base_color = unreal.MaterialProperty.MP_BASE_COLOR
grid = editing.get_material_property_input_node(material, base_color)
grid_output = editing.get_material_property_input_node_output_name(material, base_color)


def noise(scale, levels, y):
    """A noise node, from 0 to 1, with features about 1 / `scale` across."""
    node = editing.create_material_expression(material, unreal.MaterialExpressionNoise, -600, y)
    node.set_editor_property("noise_function", unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_ALU)
    node.set_editor_property("scale", scale)
    node.set_editor_property("levels", levels)
    node.set_editor_property("output_min", 0.0)
    node.set_editor_property("output_max", 1.0)
    return node


def scalar(material, name, y):
    node = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", 0.0)
    return node


# Patches about 5 meters across, mottling about 4 meters across, and grain
# about 30 centimeters across.
patches = noise(0.002, 3, 600)
mottle = noise(0.0025, 3, 750)
grain = noise(0.03, 2, 900)
mud = editing.create_material_expression(material, unreal.MaterialExpressionVertexColor, -600, 1000)
patchiness = scalar(material, "Patchiness", 1200)
detail = scalar(material, "Detail", 1300)
dirt_color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 1400)
dirt_color.set_editor_property("parameter_name", "DirtColor")
dirt_color.set_editor_property("default_value", unreal.LinearColor(0.055, 0.042, 0.026, 1.0))

ground = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -300, 800)
ground.set_editor_property("code", GROUND_CODE)
ground.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
inputs = []
for name in ["Grid", "Patches", "Mottle", "Grain", "Mud", "Patchiness", "Detail", "DirtColor"]:
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property("input_name", name)
    inputs.append(custom_input)
ground.set_editor_property("inputs", inputs)

editing.connect_material_expressions(grid, grid_output, ground, "Grid")
editing.connect_material_expressions(patches, "", ground, "Patches")
editing.connect_material_expressions(mottle, "", ground, "Mottle")
editing.connect_material_expressions(grain, "", ground, "Grain")
editing.connect_material_expressions(mud, "R", ground, "Mud")
editing.connect_material_expressions(patchiness, "", ground, "Patchiness")
editing.connect_material_expressions(detail, "", ground, "Detail")
editing.connect_material_expressions(dirt_color, "", ground, "DirtColor")
editing.connect_material_property(ground, "", base_color)

editing.recompile_material(material)
assets.save_asset(GROUND)

# With the gray grid's parameters.
instance = assets.duplicate_asset(GRAY_GRID, GROUND_INSTANCE)
editing.set_material_instance_parent(instance, material)
assets.save_asset(GROUND_INSTANCE)

#
# The glow
#

path, name = GLOW.rsplit("/", 1)
glow = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())
glow.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
glow.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
# For characters.
glow.set_editor_property("used_with_skeletal_mesh", True)

# Faint across the mesh, and bright at its edges, undoing the camera's
# exposure.
color = editing.create_material_expression(glow, unreal.MaterialExpressionVectorParameter, -600, 0)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
amount = scalar(glow, "Glow", 200)
edges = editing.create_material_expression(glow, unreal.MaterialExpressionFresnel, -600, 300)
edges.set_editor_property("exponent", 2.5)
edges.set_editor_property("base_reflect_fraction", 0.15)
tinted = editing.create_material_expression(glow, unreal.MaterialExpressionMultiply, -300, 100)
editing.connect_material_expressions(color, "", tinted, "A")
editing.connect_material_expressions(amount, "", tinted, "B")
edged = editing.create_material_expression(glow, unreal.MaterialExpressionMultiply, -150, 200)
editing.connect_material_expressions(tinted, "", edged, "A")
editing.connect_material_expressions(edges, "", edged, "B")
exposure = editing.create_material_expression(glow, unreal.MaterialExpressionEyeAdaptationInverse, -300, 400)
emissive = editing.create_material_expression(glow, unreal.MaterialExpressionMultiply, 0, 300)
editing.connect_material_expressions(edged, "", emissive, "A")
editing.connect_material_expressions(exposure, "", emissive, "B")
editing.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

editing.recompile_material(glow)
assets.save_asset(GLOW)
unreal.log("Created the demo's materials")
