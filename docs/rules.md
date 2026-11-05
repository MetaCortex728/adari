# Rules

A rule file is JSON with a `rules` array. Each rule names a template and fills in its fields.

```json
{
  "adari_rules": 1,
  "options": {"max_combinations": 4096},
  "rules": [
    {"id": "exit-reachable", "template": "reachable", "from": "entry", "to": "exit", "radius": 0.4}
  ]
}
```

Every rule has:

| Field | Meaning |
|---|---|
| `id` | Unique name; reports and SARIF use it as the rule ID |
| `template` | One of the templates below |
| `severity` | `error` (default), `warning` or `note`. Only errors make `adari check` exit with 1 |
| `description` | Free text, for people |
| `doors` | Optional door states for this rule, e.g. `{"boss_gate": "closed"}`. Doors are open unless a rule closes them |
| `within` | Optional scope selector: cells outside it are not walkable for this rule, so decorative floor outside the play area is not reported |

## Selectors

Fields such as `from`, `to` and `target` take a selector.

- `entry` matches every point and region tagged `entry`.
- `name:start` matches by name.
- `*` matches everything (used with `clear_of`).

A selector that matches nothing is not a pass. The rule is reported as "not checked", and an error-level rule that was not checked fails the run.

## Templates

| Template | Fields | Passes when |
|---|---|---|
| `reachable` | `from`, `to`, `radius` | Every point or region in `to` can be reached from `from` by an agent of `radius` (world units). Agents move in eight directions and never cut a blocked corner |
| `no_isolated_walkable` | `from`, `radius`, `min_cells` | No walkable area of at least `min_cells` cells is unreachable from `from` |
| `path_length` | `from`, `to`, `min` and/or `max`, `radius` | The shortest path is within the bounds (world units) |
| `not_visible_until` | `target`, `from`, `door` | No cell of `target` is visible from `from` while `door` is closed |
| `visible_from` | `target`, `from` | Some cell of `target` is visible from some cell of `from` |
| `near` | `subject`, `object`, `distance` | Every `subject` has an `object` within `distance` (straight line, world units). Variant options tagged like `object` count when chosen |
| `clear_of` | `region`, `blocker` | No variant option or region matching `blocker` covers a cell of `region` |
| `no_softlock` | `from`, `to`, `radius` | Every cell you can reach from `from` can still reach `to` (matters with one-way cells) |

Movement follows links too (teleporters, ladders, transitions; see the formats). A start or target drawn inside a wall, or just outside a rule's scope, counts from the open cell beside it; one that is open but too tight for the radius does not.

Visibility runs from cell centre to cell centre and is blocked by any sight-blocking cell in between, including both cells beside a corner the line passes through exactly. Large cell sets are thinned to `options.sight_samples` cells (default 256) and the report says so.

## Variants

A map can have variant groups: slots that take one option per run (optional groups can also take none). Adari checks a rule for every variant:

1. Where the rule only gets worse with more blockers (`reachable`, `visible_from`, `path_length` with `max`), it first checks the union of all options. If that passes, the rule holds for every variant; the report says `bound`.
2. Where the rule only gets worse with fewer blockers (`not_visible_until`, `path_length` with `min`), it checks with no options.
3. Otherwise, or when a bound fails, it enumerates every variant up to `options.max_combinations` (default 4096), or samples `options.sample_count` variants with `options.sample_seed` above that.
4. A failing rule reports the failing variant with the fewest chosen options, and how many variants failed.

`clear_of` checks each option on its own.
