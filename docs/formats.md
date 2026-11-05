# Map formats

Every reader produces the same model: a grid of cells with walk and sight layers, edge blocks for one-way moves, tagged points and regions, doors, and variant groups. `cell_size` converts cells to world units for radii, distances and path lengths.

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

Coordinates are in cells. Polygons cover the cells whose centres they contain. `blocks` is `walk`, `sight` or `both`.

## Tiled (`.tmj`, or `.json` saved by Tiled)

Orthogonal, finite maps with CSV tile layers.

- A cell is walkable when some tile layer has a tile there and no tile there is solid.
- A tile is solid when it has the bool property `solid` (or `collision`), when its class is `wall` or `solid`, or when it has collision shapes. It blocks sight when solid, unless `blocks_sight` says otherwise. Flip flags are ignored.
- Object layers: point objects become points; rectangles, ellipses, polygons and tile objects become regions, and a tagged region also counts as a point at its centre. The object's class (or type) and the comma-separated property `adari_tags` become tags. The bool property `door` (or class `door`) makes a door; `solid` and `blocks_sight` block cells directly; the string property `variant` = `group.option` makes a variant option.
- Map properties: `adari_cell_size` (number) and `adari_optional_groups` (comma-separated).
- Layers with the property `adari` = `ignore` are skipped.

Not yet: isometric and staggered maps, infinite maps, base64 layers, XML tilesets (`.tsx`; save them as `.tsj`), per-tile collision shapes finer than a cell.

## LDtk (`.ldtk`)

- The first IntGrid layer sets the grid. IntGrid value identifiers decide what a value means: names containing `wall`, `solid`, `collision`, `block` or `rock` block walking and sight; `water`, `pit`, `hole`, `void`, `chasm`, `lava`, `fence`, `glass` or `window` block walking only; `foliage`, `bush`, `smoke` or `curtain` block sight only. Other values are floor.
- Entities become points tagged with their identifier (also lower-cased) and their LDtk tags. Entities larger than one cell, and doors (`door` bool field or identifier `Door`), are also regions. A `name` field overrides the name; a `variant` field (`group.option`) makes a variant option.
- `--level NAME` picks a level; external level files are followed.

Not yet: several IntGrid layers with different grid sizes, layer-specific meaning overrides.
