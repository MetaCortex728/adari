# Adari

Adari checks that a game map does what its designer intended.

Level briefs already read like rules: the exit is reachable for the player's size, the boss room stays hidden until the gate opens, every spawn has cover nearby, the run takes 30 to 50 metres, no prop variant blocks the main lane, nothing can trap the player. Adari lets you write those rules down once and checks them on every map, every edit and every variant, from the editors you already use.

```
$ adari check examples/courtyard/courtyard-broken.txt --rules examples/courtyard/rules.json

  FAIL exit-reachable [reachable, error]  every variant (3/3 variants)
       fails in 1 of 3 variants checked; showing the one with the fewest options
       - point 'exit' is not reachable from 'entry' for an agent of radius 0.40  at (20, 10) ... [crates=right]
  PASS boss-hidden [not_visible_until, error]  bound (3/3 variants)
  FAIL spawn-cover [near, warning]  every variant (3/3 variants)
       - point 'spawn#2' is 5.83 from the nearest 'cover', more than 4.00  at (15, 7) ...
  ...
```

It sits between tools rather than replacing them. You keep editing in Tiled, LDtk or Unreal; navigation at runtime stays in the engine; play-testing bots stay useful for what static checks cannot model. Adari adds the rule language, one spatial model across formats, and proof that a rule holds for every variant.

## Status

Stage 1 of the [roadmap](ROADMAP.md) and the first engine front end:

- The C++17 core and the command-line tool: eight rule templates, checking across every variant, text, JSON, SARIF and PNG reports.
- Readers for Tiled (`.tmx`, `.tmj`; CSV, base64, zlib, gzip), LDtk, painted-map spatial JSON, ASCII maps and Adari JSON. They have been run against the official LDtk and Tiled sample projects; `adari inspect` shows what a reader made of a map.
- An [Unreal Engine 5.8 plugin](docs/unreal.md) that samples a level (collision or navmesh, step height, sight), checks it in the editor and draws failures.

Platformer movement, isometric Tiled maps and art-against-collision checks come next.

## Build

Needs CMake 3.21 or newer and a C++17 compiler.

```
cmake --preset default        # GCC; also clang, asan, msvc
cmake --build --preset default
ctest --preset default
```

The binary is `build/<preset>/adari` (on Windows, `build/msvc/RelWithDebInfo/adari.exe`).

## Use

```
adari check MAP --rules RULES [--level NAME | --all-levels] [--meaning NAME=MEANING]...
                [--json FILE] [--sarif FILE] [--overlay FILE.png]
adari inspect MAP [--level NAME]
adari templates
adari version
```

`adari check` prints a report and exits with 0 when every error-level rule holds, 1 when one fails or could not be checked, and 2 when the input could not be read. `--sarif` writes SARIF 2.1.0 for code-scanning tools; `--overlay` draws the map with failing cells in red and counterexample paths in orange.

- [Rules and templates](docs/rules.md)
- [Map formats](docs/formats.md)
- [Unreal plugin](docs/unreal.md)
- [Provenance](docs/provenance.md): the October 2026 work Adari continues
- [Example](examples/courtyard)

## Licence

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
