# Bridge support plan

> **Status**: rendering of authored bridges is **DONE end-to-end**
> (WP1/WP2 RE → WP4 extraction → WP5 draw pass; visually confirmed on
> ep12_bagh). What remains is **construction**: WP3 (RE the auto-bridge
> write rules) and WP6 (pathway commit + auto-bridging).

Water crossings in Trade Empires are two different mechanisms:

1. **Canal sea-mouths** are Stage-D atlas decals: a canal *endpoint* tile
   whose neighbors include shore-water draws atlas cells 45–52 (the canal
   opening into the sea) instead of a land-canal cell. **Implemented** in
   `draw_network_conn` (`kCanalMouthLUT`, byte-exact from EXE dispatch
   `0x46c3c8` + stubs `0x46c3a4`).
2. **Bridges proper** are *sprites* (`terr_brid_*` for road/trail bridges,
   `terr_rbrd_*` for rail bridges), not tile decals. A bridge tile
   (`TileConnectivity::bridge != 0xff`) *suppresses* the network decal
   (implemented), and a separate billboard pass draws the deck sprite
   (**implemented** — `GameplayScene::render_bridges()`, WP5). Roads
   dragged across water auto-create these bridges in the original
   (construction logic — **NOT implemented**; note that pathway *commit* in
   general is still preview-only in OpenTE).

## Ground truth already established (do not re-derive)

- **Connectivity bytes**: `mapp.brid` entries `{x:int16, y:int16,
  flags:uint32}` overwrite mask bytes 4/5: `bridge = flags & 0xff`,
  `bridge_aux = (flags >> 8) & 0xff` (plain `mov`s at `0x461df8/0x461dfb` —
  an earlier "OR" reading was wrong; see `03-exe-analysis.md` Round 23
  correction). 8 shipped maps have authored bridge tiles
  (ep08_anti/chan/sama, ep10_bagd, ep12_anti/bagh/hang/sogd).
- **Byte 4/5 semantics are SOLVED** (`exe_trail_re_findings.md` §0
  correction 15, from the live applier's water-crossing block
  `0x499ecc`-`0x499f73`): **byte 4 (`bridge`) = (depth[A]+depth[B])/2**,
  the averaged per-tile value of the map layer at `[map+0x2c]` (water
  depth/level — hence constant per map in the authored data: 17, 83, 35,
  1, 16, 4, 0, 26 across the 8 shipped maps; 0 is a valid depth, only
  0xff means "no bridge"). **Byte 5 (`bridge_aux`) = `0x10 <<
  cardinal_index`** from the byte table at `0x5fea00`
  (`{0,0x10,0,0x20,0,0x40,0,0x80}` — the bridge's direction; matches the
  authored 0x10/0x40 and 0x20/0x80 values).
- **Stage-D suppression**: `bridge != 0xff && (pack4(dirs of first
  non-empty network trail→road→rail) + bridge_aux) != 0` → no decal
  (EXE `0x46c082`–`0x46c0e0`; the jump target `0x46c20a` pushes page 0).
- **`BridgeMarker`** is an RTTI class in the EXE
  (`extracted/exe_rtti_classnames.txt`) — **confirmed** (WP1/Round 43) as
  the owner of the bridge sprite/lifecycle.
- **Sprites**: 20 `terr_brid_*` + 20 `terr_rbrd_*` items already dumped to
  `documentation/extracted/m_ui_terr_sprites/` (variable sizes, up to
  126×35 — multi-tile spans). Exported by the OpenTE extractor as of WP4
  (`terrain.brid.*` / `terrain.rbrd.*`; see below).

## Work packages

Each is independently completable. Suggested order: WP1 → WP2 → WP4 →
WP5 (rendering of authored bridges end-to-end — **all done**), then
WP3 → WP6 (construction — **both open**). The byte-4/5 semantics are
already solved (above), so WP1/WP2 were about the sprite object, not the
data.

Note WP6 has two halves: pathway *commit* for all four networks is
independent of WP3 and can land on its own; auto-bridging layers on top
once WP3 decodes the rules.

### WP1 — RE: `BridgeMarker` lifecycle and field map ✅ DONE (Round 43)

**Result** (`03-exe-analysis.md` Round 43): BridgeMarker (vtable `0x605184`,
0x44=68 bytes) → MapSprite → MapVisual. Constructed at exactly one site —
`TTerrainViewImp::virtual_192 @ 0x54d3b0` (ctor `0x54d5a0`), a per-tile
"add/replace marker" method, NOT the plan's guessed `0x53de70`/`0x53ddd0`
(those are generic helpers — corrected). Field map, slots, and derivation:
- **elevation** `[+0x40] = mask.byte4(depth) * [region+0x38] / 256.0`; applied
  in draw (slot 0 `0x54d670`) as `view[+0xd8] * elevation` — same `height_40`
  term as `MapSprite::virtual_0` (Round 38).
- **sprite set**: `byte2(rail)!=0 → [this+0x2d4]=m_ui/terr/rbrd`, else
  `[this+0x2d0]=m_ui/terr/brid` (loaded by `fcn.0054bdd0`).
- **variant id** (`0x54d530`) `= pack4(cardinal dirs of first non-empty net
  byte0→1→2) + byte5(aux)` — the complement of the Stage-D suppression value.
- slot 1 `0x54d650` = `x+y` isometric depth-sort key.

Original method (kept for reference), per
`documentation/06-exe-exploration-playbook.md`:
1. Look up `BridgeMarker` in `extracted/exe_vtables.txt`/`.json`: vtable
   address, every slot, ctor xref sites.
2. Disassemble its **slot-0 Persistent Load/Save virtual first** — MSVC
   `Persistent` serializers pair a `push <4cc>` with `lea reg,[obj+offset]`
   per member, giving the full field map for free (see CLAUDE.md tip; the
   anim-VM struct was recovered this way).
3. From the ctor xrefs, find who constructs BridgeMarkers: expect (a) map
   load — correlate with the `brid` loop at `0x461dad`, and (b) the pathway
   build command (WP3).
4. Deliverable: a "BridgeMarker" section in `03-exe-analysis.md` (new
   Round): field map, construction sites, and how the marker derives its
   sprite/elevation from the mask's depth (byte 4) and direction (byte 5).
   Validate against the 8 authored maps' values listed above (e.g. the
   byte-4 value should match the map's water-layer depth at those tiles).

### WP2 — RE: bridge sprite selection and drawing ✅ DONE (Round 44)

**Result** (`03-exe-analysis.md` Round 44; artifacts
`documentation/scripts/te_brid_variants.py`,
`extracted/brid_variants_validation.txt`, `extracted/exe_bridge_wp2_disasm.txt`):

- **Id scheme**: each set's 20 items are `bg6a` leaves whose *container tag
  is a raw int equal to the variant id* `pack4(dirs) + aux` (pack4: 1=N,2=E,
  4=S,8=W; aux: 0x10/0x20/0x40/0x80 = N/E/S/W). Vocabulary: `0x05/0x0a`
  straight deck middles, `0x03/06/09/0c` corner middles (diagonal
  staircases), 12 single-aux end/ramp tiles (aux points off the span, dirs
  always include aux), `0x55/0xaa` double-aux ~2-tile spans (player-build
  only). Validated 36/36 authored bridge tiles across the 8 shipped maps.
- **Lookup**: BridgeMarker ctor → `0x576510(tag=variant, flag=0x61366720)`
  child scan on the set node (`0x61366720` = the bg6a-leaf entry flag;
  `0x415fc0` was just an int-wrapper ctor) → generic media binder `0x581700`
  (bridge sprites are RGBA8888 → surface ctor `0x59f0e0`).
- **Placement**: marker `+0x2c/+0x30` = the leaf header's `off_x/off_y`
  anchor (Round 38's `anchor_x/anchor_y`, not "frame handles"). Draw =
  iso-project `(x−y, x+y)` × view half-tile scales + viewport transform +
  anchor + `ftol(view.d8)·elevation` on y; blit top-left via
  `renderer(view+0x258)->vt[+0x44]`. Depth-sort key `(x−1)+(y−1)` = one
  tile-sum behind entities on the tile (units cross over the deck).
- `futx`/`futy` (=1,1) on both sets = generic footprint-in-tiles sprite-set
  metadata (building-look consumers read it; the bridge path doesn't).
- **Bonus**: bg6a v2 pixel data starts at header `+0x24` (36-byte header, 9th
  dword at `+0x20`) — the extractors' rotate-by-4 hacks were compensating.
  **DONE**: `te_sprite.py` and `OpenTE/tools/extractor/sprites/sprite.py` now
  read pixels from `+0x24` with the rotation removed (parity-verified: output
  changes only at sprite right-edge/bottom-right corner); `game_data`
  regenerated. See `01-container-format.md` "Sprite/bitmap leaf format".

### WP3 — RE: auto-bridge on pathway drag (the live applier `0x498a90`)

Substantially advanced (see `exe_trail_re_findings.md` §0 corrections
13-15): the MakePathwayCommand path is DEAD CODE; the live pathway applier
is the giant function at `0x498a90`. Its water-crossing block
(`0x499ecc`-`0x499f73`) already gives the bridge byte semantics:
**byte 4 = averaged per-tile depth from the map layer at `[map+0x2c]`**,
**byte 5 = `0x10 << cardinal_index`** (table `0x5fea00`). The hash-write
`0x4637d0` itself spawns/removes bridge visuals: when a written mask has
byte4 != 0xff it calls into visual-update helpers (`0x53de70`/`0x53ddd0`,
after `0x463a00`-area blocks). **Correction (Round 43): those two are NOT
the BridgeMarker owner** — `0x53de70` is a ~250-caller generic helper,
`0x53ddd0` has only the 2 hash-write callers, and neither references the
BridgeMarker vtable. The actual construct/insert is
`TTerrainViewImp::virtual_192 @ 0x54d3b0` (see WP1/Round 43). Trace how the
hash-write reaches `virtual_192` (likely via the terrain view's vtable slot
holding `0x54d3b0`, `.rdata 0x604fe4`) to nail the spawn/removal edges.
Remaining questions:
1. When a dragged road/trail segment crosses water tiles, which tiles get
   `bridge`/`bridge_aux` written, and with what values?
2. Constraints: maximum span, straight-only?, allowed terrain, cost
   multiplier (cross-check `epis.<ep>.tech` pricing fields and the
   transporter chart in `extracted/`).
3. Where the BridgeMarker is spawned (ties into WP1 ctor xrefs).
4. For CANALS: the diagonal flank-tile writes are already decoded
   (correction 14 — perpendicular 3-bit fans, implemented in the preview);
   what remains is how terrain flag bit 0x40 gets set on canal tiles
   (CanalFinderIterator routes boats via terrain bytes, not the mask; note
   the hash-write `0x4637d0` ORs terrain bit 0x10 on every network write —
   0x40 may be set similarly in a canal-specific spot).
Existing artifacts (in `documentation/extracted/`) — read before starting:
- `exe_pathapply_498a90_region.txt` — resync disasm of `0x498300`-`0x49b100`
  covering the LIVE applier (the crown-jewel function; only ~40% has been
  read closely — the cardinal/generic block, bend handling, and the code
  after the flank writes at `0x49a14f`-`0x49a2d8` are still unread).
- `exe_makepathway_execute_54a4a0.txt` / `exe_pathcmd_watersplit_4bbc10.txt`
  — the DEAD command path (still useful as a simpler reference for the
  same per-step logic, and the water-run splitting idea).
- `exe_pathway1.py`–`exe_pathway3.py` + `exe_pathway1_dump.txt` (older
  drag-out rules RE).

### WP4 — Extractor: export bridge sprites ✅ DONE

**Result**: `extract_bridge_sprites()` added to
`OpenTE/tools/extractor/sprites/terrain.py`, wired into `main.py` after the
terrain-texture pass (reuses the already-loaded `m_ui,u.{}`). Same pattern as
`decorations.py` (raw-int-tagged `bg6a` leaves): iterates `terr/brid` and
`terr/rbrd`, keeps only children with the `bg6a`-leaf flag `0x61366720` (skips
the `futx`/`futy` int fields and any "+1 spillover" header), recovers the
variant id from the leaf's raw-int tag (`struct.unpack('<I',
tag_bytes[::-1])`, range-guarded `0..0xaa`), and writes each via the existing
`decode_sprite` (bpp==4 RGBA8888) → `write_png_rgba`.

- **Output**: 40 PNGs (20 per set) under `game_data/sprites/terrain/`, ids
  `terrain.brid.<variant>` / `terrain.rbrd.<variant>` (variant in decimal),
  with `anchor_x/anchor_y` = the leaf's off_x/off_y carried in each
  `SpriteEntry` (WP5 placement anchor). Plus a variant→id lookup table
  `game_data/tables/bridges.json`:
  `{"sets": {"brid": {"<variant>": "terrain.brid.<variant>", ...}, "rbrd": {…}}}`
  registered as manifest table `bridges`.
- **Verified** against the real `m_ui,u.{}`: both sets yield exactly 20
  sprites; variants `{3,5,6,9,10,12,19,21,25,35,38,42,69,70,76,85,137,138,140,170}`
  match WP2's vocabulary (straight `5`/`10`, corners `3`/`6`/`9`/`12`, 12
  single-aux ramps, double-aux spans `85`=0x55 / `170`=0xaa at the widest
  126×35 / 122×26). Decoded RGBA is non-empty (e.g. `brid.5` 41% opaque).
- **Note**: uses `decode_sprite`, which now reads the 36-byte header with
  pixels at `+0x24` (the WP2 "bonus" bg6a-v2 header fix — the old rotate-by-4
  hack has been removed). Bridge sprites re-extracted with the corrected
  decoder; the change affects at most the far-right column / bottom-right
  corner of each sprite.

### WP5 — Game: draw bridges ✅ DONE

**Result**: `GameplayScene::render_bridges()` added
(`OpenTE/game/src/gameplay/gameplay_scene.cpp`), called in `render()` after
the terrain + construction overlays and before decorations/buildings (the
deck sorts one tile-sum behind entities per WP2). Modeled on
`render_decorations()`:

- **Sprite load** (`load_sprites`): all manifest sprites with id prefix
  `terrain.brid.` / `terrain.rbrd.` are loaded into `bridge_sprites_` (an
  `AnchoredSprite` map, same as `decoration_sprites_`), carrying the leaf's
  `anchor_x/anchor_y`.
- **Per-tile selection**: for every tile with `connectivity.bridge != 0xff`,
  `variant = extract4(first-non-empty of trail→road→rail) + bridge_aux`
  (bit-identical to the Stage-D suppression value in
  `draw_network_conn`), `set = rail ? "rbrd" : "brid"`, sprite id
  `"terrain."+set+"."+variant`. `variant == 0` or a missing sprite is
  skipped.
- **Draw**: billboard at `tile_to_world(tx,ty)` (= WP2's `(x−y, x+y)`
  half-tile iso-projection) minus `elevation·kPixelsPerAltiUnit`, blit
  top-left = `world_to_screen + anchor·zoom` — the same anchor convention as
  decorations/buildings. Bridge tiles are gathered and painter-sorted
  back-to-front by `tx+ty` so overlapping multi-tile deck spans layer
  correctly.
- **Elevation** = the mask's byte-4 **depth** (uniform across a bridge's
  tiles), NOT per-tile terrain height. Per WP1 the EXE treats the depth byte
  exactly as an alti byte (`depth * (region+0x38 = 10) / 256`, the same
  conversion as terrain height — Round 43 / `03-exe-analysis.md:2174`), so
  the deck y-offset is `depth · kPixelsPerAltiUnit`. This keeps the span
  perfectly level; an earlier version sampled the terrain height per tile,
  which stepped each deck sprite by the local slope and left a 1px seam at
  the joins (e.g. ep12_bagh's 4-tile road bridge, terrain vertex heights
  3/2/1/3 but depth a constant 4).

Data pipeline confirmed end-to-end: extractor writes deck PNGs + manifest
entries (WP4) and per-tile `bridge`/`bridge_aux` bytes (`maps/region.py`);
`region.cpp` decodes all 6 connectivity bytes; `game_data/` already carries
the 40 deck sprites. Builds clean and launches without crashing.

**Visual confirmation pending** (needs a user screenshot, per CLAUDE.md — no
computer-use): load a map with authored bridges — **ep08** (anti/chan/sama),
**ep10** (bagd), or **ep12** (anti/bagh/hang/sogd) — and check the deck
sprites sit on the water crossings where the road/rail meets shore.

### WP6 — Game: pathway commit + auto-bridging

Pathway construction currently renders previews only — there is no commit
that mutates `Region::connectivity_`. Implement commit for all four
networks (update both endpoints' direction bits per
`spec/world-and-maps.md` §"Building/extending a pathway segment"), then add
the WP3 auto-bridge rules for water crossings.

## Traps to avoid (all have burned sessions before)

- Don't infer footprints/spans/costs from sprite pixel sizes (CLAUDE.md).
- Don't trust single-pass linear capstone disassembly — use
  `exe_disasm_range.py` (resync) or function bounds from
  `exe_functions_aaa.json`.
- Check `extracted/exe_vtables.txt` BEFORE any r2/manual hunt for "who
  calls/implements X".
- First-pass RE names are hypotheses: the mask's byte 2/3 were mis-named
  canal/rail for several sessions (they are rail/canal —
  `path-rendering-handoff.md` §0). Validate any name attribution against
  player-facing meaning (art, tech era, in-game behavior) before building
  on it, and decode the consumer's slot arithmetic rather than assuming
  tag = content.
