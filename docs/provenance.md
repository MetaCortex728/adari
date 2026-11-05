# Provenance

Adari's code is new: this repository starts on 5 November 2026, after the programme invitation that prompted publishing it (29 October 2026, 19:30 UTC). What it continues is a problem and an approach that Han worked out in October 2026 for the painted maps of his own Unreal Engine 5.8 game, with OpenAI Codex as a coding assistant and without version control: map layouts built in Blender (ground, paths, boundaries, routes) with an offline check of route clearance, then a hand-authored spatial layer over each painting (walk areas, object footprints, covers, sight blockers, routes), a design that separates three meanings of blocking (movement, combat sight, camera cover), and checks that kept the layer consistent with the art (a grid reachability checker, boundary QA runs in the engine, a review overlay, engine navigation and its tests). Adari generalises those checks into rules any designer can write, for maps from several editors.

This page lists the evidence for that earlier work, says what kind of evidence each item is, and what it does and does not show. All times are UTC.

## The layouts (28 October, from 12:06)

Three Blender scripts laid out the maps before their paintings existed: a full camp map, a harbour quarter of individually composed structures with footprints, and the four main-loop map layouts. A validation run of the harbour quarter checked its routes against the structures' footprints, an early form of the reachability-with-clearance check that Adari now runs for any map.

| File | First recorded write | Last recorded write | Replay vs the current file | SHA-256 now |
|---|---|---|---|---|
| `exports/byeokrando-camp-full-map/build_map.py` | 2026-10-28 12:06:26 | 2026-10-28 12:16:01 | same SHA-256 | `6db4b38d21f6b0a9c2f4219f132f69a6ab2d77115663daaa7797b17e1f7eb47d` |
| `exports/byeokrando-harbor-quarter-r2/build_map.py` | 2026-10-28 12:50:11 | 2026-10-28 12:50:43 | same SHA-256 | `7e1c6bdd61bdfe6e0286a35a4a27f00abf495f8e0df25dc29a2eb9f889bb06ad` |
| `exports/dan-main-loop-maps-v2/build_layouts.py` | 2026-10-28 13:21:21 | 2026-10-28 13:22:27 | same SHA-256 | `7a3c9b429d12f34ac0cd9b3b47f4a1fff2a203a903dbe4b89c13e677d1c61cbe` |

Recorded runs, from the same session (`01a13efd-4a4b-7283-a42c-728222aa7224`): Blender built the camp (12:09:40, 146 objects), the harbour quarter (12:53:21) and the four main-loop layouts (13:21:56, again 13:26:17), each with exit code 0. At 13:04:05 the harbour validation passed: 8 places, 4 routes and 1,344 centre-line samples, each at least 0.35 m from the listed rectangular footprints (not full-width or engine collision). [`provenance/session-records-layouts.json`](provenance/session-records-layouts.json) lists these writes and runs with line numbers and call ids, without source code, conversation text or local paths.

The records also show how the layouts led to the spatial layer. Han narrowed the camp to one part of the port (12:19) and asked for a multinational, multicultural scene (13:09); at 14:18 he asked that walkable and blocked ground be distinguishable at a glance. The assistant then checked each route against the paintings, found paths the paintings showed as open and a quay painted as accessible but disconnected in the movement data (14:22 to 14:28), and the first `spatial.json` edit followed at 14:31. The records do not show coordinates being exported automatically from Blender into `spatial.json`. An earlier visual export of five concept scenes (09:46) exists but is not a layout or a check.

## The spatial layer and its checks (28 and 29 October, from 14:31)

Paths are relative to the game folder. Times and hashes are the filesystem's, read on 5 November 2026; they are local and can be changed, so they are supporting evidence, not proof. None of these files is copied into this repository; Adari reimplements the ideas in C++.

| File | Created | Last written | SHA-256 |
|---|---|---|---|
| `exports/dan-main-loop-maps-v2/check-spatial.mjs` | 2026-10-28 14:31 | 2026-10-29 00:34 | `f9e837ba9a141464269d4ed929174ba20be8fda300ce05acfab302147cfaa6a5` |
| `exports/dan-main-loop-maps-v2/spatial-baseline.json` | 2026-10-28 14:31 | 2026-10-28 14:31 | `0369a4777e6696d1a31c515d2caec99d7e691f273184c37762a893ca5f247941` |
| `exports/dan-main-loop-maps-v2/build-spatial-review.mjs` | 2026-10-28 14:45 | 2026-10-28 17:22 | `207718da80348ae931ebb65d2d1fddc9ea477859cfbd926ad4b7b6a6dadcad20` |
| `exports/dan-main-loop-maps-v2/spatial-review.template.html` | 2026-10-28 14:45 | 2026-10-28 20:05 | `29af515d1088832eb9bf4bb92f4bba1300cda513c85f1cdbbd1e31fc325ab732` |
| `exports/dan-main-loop-maps-v2/SPATIAL_DESIGN.md` | 2026-10-28 14:55 | 2026-10-29 09:26 | `a720edfb46bdf6bf7ac364dc4843f7496fc5e72345acfbe851de553376c58e15` |
| `exports/dan-main-loop-maps-v2/verify-spatial-review.mjs` | 2026-10-28 14:56 | 2026-10-28 20:07 | `57274b623c7a2046cd6716eeb5109ec21259feb469688055b162140d2fbd8cd3` |
| `prototype/GyeolLab/Source/GyeolLab/MainLoop/MainLoopSight.h` | 2026-10-28 16:20 | 2026-10-28 16:20 | `cbfb94549d91226cc18c3c832c03a1cbbda120f66e1f87f00f7dfb574ff790b2` |
| `exports/dan-main-loop-maps-v2/spatial-geometry.mjs` | 2026-10-28 18:29 | 2026-10-28 18:29 | `65b0743a80945b59237d755666459f185cbbe330b6c1819d25acb852305f4c6b` |
| `exports/dan-main-loop-maps-v2/test-spatial-geometry.mjs` | 2026-10-28 18:29 | 2026-10-28 18:33 | `fc6a7a21bc624be807b165d5d5764a2f8e56b82522e621537347b1e72d6ebc4e` |
| `prototype/GyeolLab/Source/GyeolLab/MainLoop/MainLoopBoundarySelection.h` | 2026-10-28 22:42 | 2026-10-28 22:42 | `ae98734fdb34593568082afa9852b5d108d8dd1147182c53ae631e21d64874d4` |
| `prototype/GyeolLab/Source/GyeolLab/MainLoop/MainLoopNavigation.h` | 2026-10-29 03:54 | 2026-10-29 04:14 | `82660c943f4df157e990450ffeeb2ff95101c632428da2d6dac76e4336a89277` |
| `prototype/GyeolLab/Source/GyeolLab/MainLoop/MainLoopNavigation.cpp` | 2026-10-29 03:54 | 2026-10-29 14:18 | `8613f7ad3a9983370e882c00d9e8e1f46eaeea9509bc1110e558f839e39a8656` |
| `prototype/GyeolLab/Source/GyeolLab/Tests/MainLoopNavigationTests.cpp` | 2026-10-29 03:55 | 2026-10-29 04:15 | `5446866ff26955514351d291e73ff1813e95c6081752638b0b3155e88b88086d` |
| `exports/dan-main-loop-maps-v2/spatial.json` | 2026-10-30 02:33 | 2026-10-30 03:35 | `6b99c67d6ab57f295685bac562ebc07a8dac862fed59fc646cc0656627f9664f` |

`spatial.json` was rewritten on 30 October, after the invitation; its earlier states survive as the `before` and `initial` snapshots of each revision folder, the last of which (by modification time) was written at 2026-10-29 03:19 (`277fc6ada1ff981261d8f4082a170d1df5c3237cb3afc9133e65848183f64689`).

## The assistant's session records

OpenAI Codex keeps a local record of each session with every file edit and command. On 5 November 2026 the assistant read the October segments of the session in which this work was done (`01a13efd-4a4b-7283-a42c-728222aa7224`, plus small 28 and 29 October segments filtered for these files), counted only entries before the invitation, and replayed the recorded edits. [`provenance/session-records.json`](provenance/session-records.json) lists 236 recorded edits, 200 recorded commands with result markers, 93 check results kept in revision folders, and the snapshot hashes, with session line numbers and call ids. It holds no source code, conversation text or local paths.

| File | First recorded write | Last recorded write before the invitation | Replay up to the invitation vs the current file |
|---|---|---|---|
| `check-spatial.mjs` | 2026-10-28 14:31:23 | 2026-10-29 00:34:09 | same SHA-256 |
| `build-spatial-review.mjs` | 2026-10-28 14:45:38 | 2026-10-28 17:22:25 | same SHA-256 |
| `spatial-review.template.html` | 2026-10-28 14:45:38 | 2026-10-28 20:05:48 | same SHA-256 |
| `SPATIAL_DESIGN.md` | 2026-10-28 14:55:51 | 2026-10-29 09:26:22 | content not fully recorded |
| `verify-spatial-review.mjs` | 2026-10-28 14:56:52 | 2026-10-28 20:07:58 | same SHA-256 |
| `MainLoopSight.h` | 2026-10-28 16:20:13 | 2026-10-28 16:20:13 | same SHA-256 |
| `spatial-geometry.mjs` | 2026-10-28 18:29:27 | 2026-10-28 18:29:27 | same SHA-256 |
| `test-spatial-geometry.mjs` | 2026-10-28 18:29:27 | 2026-10-28 18:33:20 | same SHA-256 |
| `MainLoopBoundarySelection.h` | 2026-10-28 22:41:59 | 2026-10-28 22:41:59 | same SHA-256 |
| `MainLoopNavigation.h` | 2026-10-29 03:54:27 | 2026-10-29 04:14:49 | same SHA-256 |
| `MainLoopNavigation.cpp` | 2026-10-29 03:54:27 | 2026-10-29 14:18:51 | same SHA-256 |
| `MainLoopNavigationTests.cpp` | 2026-10-29 03:55:31 | 2026-10-29 04:15:56 | same SHA-256 |
| `spatial.json` | 2026-10-28 14:31:23 | 2026-10-29 03:16:56 | content not fully recorded |
| `MainLoopMode.cpp` (holds the boundary QA entry point) | 2026-10-28 13:36:36 | 2026-10-29 14:18:51 | content not fully recorded |
| `spatial-baseline.json` | no recorded edit; filesystem only | | |

"Same SHA-256" means replaying the recorded edits reproduces the current file exactly, so the file has not changed since its last recorded edit before the invitation. A recorded edit is the text the assistant proposed; it does not by itself certify that the write reached the disk.

**Runs.** The records hold commands with result markers from 28 and 29 October, including boundary QA runs in the engine that print `MAIN_LOOP_BOUNDARY_QA PASS` and spatial checker runs whose results were kept in the revision folders. An exit code alone is not a passed spatial assertion; the file lists each marker as recorded. An automation report in the revision folders (`continuous-route-r1`, report clock 2026-10-29 04:16) shows 76 tests passed and 0 failed, including `Gyeol.MainLoop.ContinuousRoute` and `Gyeol.MainLoop.RouteCue`; its link to a particular session was not established.

**Who decided what.** At 2026-10-28 14:18:19 Han asked for a design of how objects interact with the player's line of sight, and for walkable and blocked ground to be distinguishable at a glance. At 14:34:31 and 14:52:22 the assistant described the resulting layers: solid footprints under buildings, trunks and props, combat sight blockers, a foreground cover test, and separate layers for walkable ground, footprints, camera cover and the route. The records do not establish who first proposed the exact cover-anchor, slope or fitted-volume formulas.

## What this does and does not show

- It shows, from the assistant's local session records and the files' current hashes, that the map layouts (from 12:06 UTC on 28 October), a route clearance check (13:04), the spatial layer, its design and its checks were written on 28 and 29 October 2026, before the invitation, and that the layout scripts and most of the checker files are unchanged since.
- It does not show a server-certified date: no server-side record was queried, and local records and file times can be changed.
- It does not show that Adari's code existed before the invitation. Adari is new code written from 5 November 2026; it continues the October problem and approach, not the October files.
- No map-specific layout or collision check earlier than 28 October 12:06 UTC was found in the examined records; earlier animation-loop boundary files are not map checks. The search covered the identified session family, not every chat.
