# Adari roadmap

Adari (아다리) checks that a game map does what its designer intended. Designers declare intent as rules: every exit reachable from the entry for an agent of a given radius, a room not visible before its door opens, cover near every spawn, path length within a range, no softlock. Adari reads maps from the editors teams already use, evaluates the rules on every variant and seed, and reports failures as overlays, SARIF and JSON for CI and editors.

It sits between tools rather than replacing them. Editing stays in Tiled, LDtk, Godot, Unity or Unreal; navigation at runtime stays in the engine; agent bots stay complementary. Adari's own value is the rule language, the common spatial model across formats, and the proof that a rule holds for every variant.

## Design decisions

| Decision | Choice | Why |
|---|---|---|
| Language | C++17 core, no engine types | Same shape as Hanji: one core, CI on GitHub runners, thin front ends for Unreal, Godot (GDExtension) and Unity (native plugin or CLI) |
| Reference backend | Uniform grid with per-cell layers | Light and faithful for 200x200 tile maps and for painted maps resampled to a grid; BFS, distance transforms and ray sampling are simple and testable. A polygon backend can be added where grid rounding is wrong |
| Rule format | Tags placed in the editor plus a small rule file of template instances (`id`, `template`, selectors, parameters, `severity`) | The shape building-code checkers use (Solibri rule sets, buildingSMART IDS). JSON first; YAML later |
| Reports | JSON, SARIF 2.1.0, PNG overlay; exit code for CI | SARIF is what code-scanning UIs already read; overlays are what designers read |
| Write-back | Sidecar files only in stage 1 | Re-saving a user's map through a lossy reader can drop data (for example Tiled drops unknown TMX elements) |
| Licence | Apache-2.0 | Same as the other repositories |

## Stage 1: core and first readers

1. Repository, CMake presets, CI (Linux GCC, Clang, ASan; Windows MSVC), vendored doctest and nlohmann_json.
2. Common spatial model: grid with walk and sight layers, cell size in world units, tagged points and regions, doors as switchable regions, variant groups whose options carry their own blocking footprints.
3. Grid algorithms: clearance by distance transform (agent radius), reachability and connected regions, shortest path length, line of sight by supercover rays, region-to-region visibility.
4. Rule templates and evaluator:
   - `reachable` — every target tag reachable from a source tag for a given agent radius.
   - `no_isolated_walkable` — no walkable region larger than a threshold that cannot be reached from the entry.
   - `path_length` — shortest path between tags within [min, max].
   - `not_visible_until` — a region is not visible from another while a door is closed.
   - `visible_from` — a target is visible from a source (for example a reward from the entry).
   - `near` — every point with one tag has a point or region with another tag within a distance.
   - `clear_of` — no region with one tag overlaps cells reserved by another (for example props on a critical path).
   - `no_softlock` — every cell reachable from the entry can still reach an exit (meaningful once one-way moves exist).
5. Variants: evaluate the monotone bounds first (all optional blockers present for reachability, none present for hiding rules), enumerate combinations only when a bound fails, report the smallest failing combination.
6. Readers: Adari JSON (native), ASCII maps (for tests and quick sketches), Tiled TMJ (orthogonal first: collision from tile properties and per-tile collision objects, objects as tagged points and regions), LDtk (IntGrid values as walk and sight, entities as tagged points).
7. CLI `Aadari check`, reports (JSON, SARIF, PNG overlay), exit codes.
8. Tests: each template with a passing and a failing ASCII map, reader fixtures, a small sample project.
9. README and docs (model, rules, readers, reports).

Acceptance for stage 1: every template has a failing fixture that it catches and a passing one it accepts; Tiled and LDtk fixtures load to the same model as an equivalent ASCII map; the CLI returns a non-zero exit code and a SARIF file on failure; CI is green on all four presets.

## Stage 2: Unreal, real maps and movement

Unreal is the only engine front end for now (decided 2026-11-05); Godot and Unity wait until it is solid.

- Done: Unreal 5.8 plugin with a sampling volume, markers (tags, doors, variant props), collision or navmesh sampling with step height and sight, `Adari.Check`, debug drawing, reports and Adari JSON export, headless automation tests.
- Done: TMX and compressed tile data, LDtk fixes found on the official samples, `adari inspect`, `--all-levels`, reader meanings, painted-map spatial JSON.
- Unreal next: a persistent editor-mode overlay, a Data Validation hook on save, rotated volumes and several floors.
- Done: links (teleporters, ladders) in every reader and the Unreal markers; the LDtk Entities sample now passes through its teleporters.
- Boundary tests from painted-map files (start, direction, expected stop box).
- Done: rule scopes (`within`).
- Isometric and staggered Tiled maps; Tiled flip bits applied to per-tile collision.
- Directed edges and one-way moves (RPG Maker passability, ledges, drops); softlock detection using forward, backward and sink labels.
- Platformer movement as templates compiled from physics constants; ability-gated reachability by sphere search.
- Sampled seeds: a generator hook that emits maps, checked headless with failing seeds kept as fixtures.
- Later: Godot 4 GDExtension (headless Godot can run in GitHub CI) and a Unity editor exporter.

## Stage 3: art against data, and more formats

- Deterministic art checks: tile alpha against collision flags, traced alpha polygons against collision polygons, art changed since the data was last edited.
- Foreground occlusion: share of walkable area hidden behind foreground art.
- Optional suspect ranking from segmentation or depth models; never fails a build.
- RPG Maker MZ, Aseprite slices, Ogmo; SAT-based variant search when slots exceed about twenty.
- Write-back markers inside editors (Tiled extension, Godot plugin).

## Not planned

Level generation, agent bots, runtime navigation, editing tools. Adari reads and checks; other tools author and play.
