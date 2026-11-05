# Unreal Engine plugin

`unreal/Adari` is an Unreal Engine 5.8 plugin that samples a level into Adari's grid model, checks a rule file against it, draws failures in the level and writes the same reports as the command-line tool. The core is compiled into the plugin from `core/`, so clone the whole repository (or add it as a submodule) and point the project at `unreal/` with `AdditionalPluginDirectories`, as `unreal/AdariSample` does.

## In a level

1. Place an **Adari Volume** around the area to check. Its top should sit just above the highest walkable floor and below the tops of walls; the box is read axis-aligned.
2. Add **Adari Marker** components to actors that rules talk about, and give them tags: `entry`, `exit`, `spawn`, `cover`, `boss`. A Box marker covers an area; a Point marker is one spot. An actor tag such as `adari:entry` works as a point too.
   - **Door**: tick `bDoor`. The door's actor is left out of sampling, so it is open unless a rule closes it (`"doors": {"boss_gate": "closed"}`).
   - **Variant prop**: set `Variant` to `group.option` (for example `crates.left`). The prop's actor is left out of sampling and its collision becomes that option; Adari checks the rules for every combination.
3. Set the volume's **RulesFile** (relative to the project) or write rules inline in **RulesJson**.
4. Run `Adari.Check` in the console, or call **Check Volume** / **Check All** from Blueprint or Python.

Failing cells are drawn red (warnings amber) and counterexample paths orange for `DrawSeconds`. Reports go to `Saved/Adari/<volume>/`: `report.txt`, `report.json`, `report.sarif`, `overlay.png`, and `map.adari.json`, the sampled level, which `adari check` can check again outside the engine.

## How a level is sampled

For each cell centre, from the volume's top down:

| Question | How | Volume setting |
|---|---|---|
| Is there ground? | Line trace on the movement channel | `MovementChannel` (Pawn) |
| Is it too steep? | Ground normal against the slope limit | `MaxSlope` (44°) |
| Does the agent fit? | Box overlap from `StepHeight` up to `AgentHeight` above the ground | `AgentHeight`, `StepHeight` |
| Does the navmesh agree? | When there is a navigation mesh and `bUseNavMesh` is set, it decides walkability instead | `bUseNavMesh` |
| Does it block sight? | Thin box overlap at `EyeHeight` on the sight channel | `EyeHeight`, `SightChannel` (Visibility) |
| Can you step between neighbours? | A ground height difference above `StepHeight` blocks the step | `StepHeight` |

Cell size is `CellSize` centimetres, and rule radii, distances and path lengths are in centimetres.

## Building and testing

GitHub's runners have no Unreal Engine, so the plugin is built and tested locally:

```
powershell -File unreal/adari-unreal.ps1            # build AdariSampleEditor, then run the Adari.* automation tests headless
powershell -File unreal/adari-unreal.ps1 -Mode Build
```

The tests build small levels from collision boxes (an open room, a wall that splits it, a gap that fits a narrow agent but not a wide one, a variant crate in the gap, a step and a ledge) and check the sampled grid and the rule results, plus a round trip through Adari JSON.

## Not yet

- Rotated volumes, several floors in one volume, and moving platforms.
- An editor-mode overlay that stays on screen, and a Data Validation hook that runs on save.
- Links such as teleporters and ladders between distant cells.
