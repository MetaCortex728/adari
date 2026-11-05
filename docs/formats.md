# Map formats

Every reader produces the same model: a grid of cells with walk and sight layers, edge blocks for one-way moves, tagged points and regions, doors, and variant groups. `cell_size` converts cells to world units for radii, distances and path lengths.

Run `adari inspect MAP` first on an unfamiliar map. It prints what the reader made of it (size, walkable and sight-blocking share, separate walkable areas, tags, regions, doors, variant groups) and any notes, for example that no tile carries collision data.

## ASCII (`.txt`)

For tests, sketches and bug reports.

```
name courtyard
cell_size 1
legend S point entry spawn      # name, then extra tags
legend c region cover
legend D door boss_gate
legend 1 option crates.left     # blocks walk and sight when chosen
legend 2 option crates.right walk   # blocks walk only
group crates optional
map
#########
#S..c..D#
#..1.2..#
#########
```

Built-in cells: `#` and space are walls, `.` floor, `~` blocks walking but not sight (water, a low fence), `%` blocks sight but not walking (tall grass). `v ^ < >` are one-way cells: you can cross them only in the arrow's direction. Findings point back to file lines and columns.

## Adari JSON

```json
{"adari_map": 1, "name": "yard", "cell_size": 0.5,
 "rows": ["#####", "#S..#", "#####"],
 "legend": {"S": "point entry"},
 "optional_groups": ["crates"],
 "points":  [{"name": "exit", "tags": ["exit"], "x": 3.5, "y": 1.5}],
 "regions": [{"name": "pit", "tags": ["hazard"], "polygon": [[2,1],[3,1],[3,2],[2,2]], "door": false}],
 "options": [{"group": "crates", "name": "a", "tags": ["cover"], "polygon": [[1,1],[2,1],[2,2],[1,2]], "blocks": "both"}]}
```

Coordinates are in cells. A region or option covers either a `polygon` (cells whose centres it contains) or an explicit `cells` list of `[x, y]`; options can also give `block_walk_cells` and `block_sight_cells`. `blocks` is `walk`, `sight` or `both`. This is also the format the Unreal plugin writes for a sampled level.

## Tiled (`.tmx`, `.tmj`, or `.json` saved by Tiled)

Orthogonal, finite maps. Tile data may be CSV, XML, or base64 (uncompressed, zlib or gzip); tilesets may be embedded, `.tsx` or `.tsj`.

- A cell is walkable when some tile layer has a tile there and no tile there is solid. A map with no tile layers at all (built only from objects) treats empty cells as open space.
- A tile is solid when it has the bool property `solid` (or `collision`), when its class is `wall` or `solid`, or when it has collision shapes. It blocks sight when solid, unless `blocks_sight` says otherwise. Flip flags are ignored.
- Object layers: point objects become points; tagged rectangles, ellipses, polygons and tile objects become regions. The object's class (or type) and the comma-separated property `adari_tags` become tags; untagged shapes carry no meaning unless they block. The bool property `door` (or class `door`) makes a door; `solid` and `blocks_sight` block cells directly, and a tile object blocks like its tile; the string property `variant` = `group.option` makes a variant option. Polylines are ignored.
- Map properties: `adari_cell_size` (number) and `adari_optional_groups` (comma-separated).
- Layers with the property `adari` = `ignore` are skipped.

Not yet: isometric, staggered and hexagonal maps, infinite maps, zstd compression, per-tile collision shapes finer than a cell.

## LDtk (`.ldtk`)

- The IntGrid layer whose name contains `collision`, `wall` or `solid` sets the grid (otherwise the first IntGrid layer). Other IntGrid layers, even with a different grid size, are resampled onto it.
- IntGrid value identifiers decide what a value means. Names containing `floor`, `ground`, `grass`, `path`, `road`, `ladder`, `bridge` or `stair` are floor; `wall`, `solid`, `collision`, `block` or `rock` block walking and sight; `water`, `pit`, `hole`, `void`, `chasm`, `lava`, `fence`, `glass`, `window` or `spike` block walking only; `foliage`, `bush`, `smoke` or `curtain` block sight only. Any other value in a collision layer is a wall, and elsewhere floor.
- Cells where the level draws nothing at all (no IntGrid value and no tile in any layer) are void, not floor.
- Entities of one cell or less are points; larger entities and doors (a `door` bool field or the identifier `Door`) are regions. Tags are the identifier (also lower-cased) and the entity's LDtk tags. A `name` field overrides the name; a `variant` field (`group.option`) makes a variant option.
- `--level NAME` picks a level, `--all-levels` checks each one; external level files are followed.

## Saying what names mean

When a project's names don't match the guesses above, give the meanings in the rule file or on the command line. Keys are LDtk IntGrid identifiers or Tiled tile classes, matched case-insensitively; values are `wall`, `low` (blocks walking only), `sight` (blocks sight only) or `floor`.

```json
{"reader": {"meanings": {"dirt": "wall", "stone": "wall", "shallows": "low"}}, "rules": [ ... ]}
```

```
adari check level.ldtk --rules rules.json --meaning dirt=wall --meaning shallows=low
```

The command line wins over the rule file.

## Painted-map spatial JSON

For games whose levels are paintings with a separately authored gameplay layer. One file holds several maps; coordinates are normalized to the painting (0 to 1).

```json
{"schema": 1, "maps": [
  {"walk": [[[x, y], ...], ...],
   "objects": [{"id": "well", "kind": "well", "footprint": [[x, y], ...], "movement": true, "combatSight": false}],
   "covers": [{"id": "roof", "anchor": [x, y], "groundSlope": 0, "polygon": [[x, y], ...]}],
   "route": [[x, y], ...], "exit": [x, y], "destination": "Next map",
   "destinations": [{"name": "Old Well", "point": [x, y]}]}]}
```

- Walk polygons are the walkable area. Object footprints block movement (`movement`) and/or sight (`combatSight`) and are also regions tagged `object`, their kind, `solid` and/or `sight_blocker`.
- Covers are regions tagged `cover` and `foreground`. The route's first point is a point tagged `start` and `entry`; the route itself is a region tagged `route`. The exit is a point tagged `exit`; destinations are points tagged `destination` and a slug of their name.
- The painting is sampled on a 384 x 256 grid with `cell_size` 16, so world units are pixels of a 6144 x 4096 painting. Maps are levels `map0`, `map1`, ...; `--all-levels` checks each.
- `boundaryTests` in the file are reported but not checked yet.
