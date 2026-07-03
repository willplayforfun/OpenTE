# Bridge support plan

Water crossings in Trade Empires are two different mechanisms, and one of
them is already done:

1. **Canal sea-mouths** are Stage-D atlas decals: a canal *endpoint* tile
   whose neighbors include shore-water draws atlas cells 45–52 (the canal
   opening into the sea) instead of a land-canal cell. **Implemented** in
   `draw_network_conn` (`kCanalMouthLUT`, byte-exact from EXE dispatch
   `0x46c3c8` + stubs `0x46c3a4`).
2. **Bridges proper** are *sprites* (`terr_brid_*` for road/trail bridges,
   `terr_rbrd_*` for rail bridges), not tile decals. A bridge tile
   (`TileConnectivity::bridge != 0xff`) *suppresses* the network decal
   (implemented), and something else draws the bridge sprite (NOT
   implemented). Roads dragged across water auto-create these bridges in the
   original (construction logic — NOT implemented; note that pathway
   *commit* in general is still preview-only in OpenTE).

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
  (`extracted/exe_rtti_classnames.txt`) — the presumed owner of the bridge
  sprite/lifecycle.
- **Sprites**: 20 `terr_brid_*` + 20 `terr_rbrd_*` items already dumped to
  `documentation/extracted/m_ui_terr_sprites/` (variable sizes, up to
  126×35 — multi-tile spans). Not yet exported by the OpenTE extractor.

## Work packages

Each is independently completable. Suggested order: WP1 → WP2 → WP4 →
WP5 (rendering of authored bridges end-to-end), then WP3 → WP6
(construction). The byte-4/5 semantics are already solved (above), so
WP1/WP2 are about the sprite object, not the data.

### WP1 — RE: `BridgeMarker` lifecycle and field map

Method (per `documentation/06-exe-exploration-playbook.md`):
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

### WP2 — RE: bridge sprite selection and drawing

1. Find the `terr/brid` / `terr/rbrd` container nodes (same `m_ui` scope as
   the network atlases; `documentation/scripts/te_sprite.py` dumps them).
   Item count is 20 per set — determine the variant scheme (direction ×
   span-length × end-caps is the likely split given the sizes; do NOT infer
   gameplay values from the pixel sizes, only the variant *inventory*).
2. In the EXE, find who reads those nodes (same `FindRecordByTag` idiom as
   the atlas loader `0x466790`; search `.text` for the reversed-4cc
   immediates `'brid'`/`'rbrd'` used as `push` operands near `0x576510`
   calls) and how a BridgeMarker picks its item + screen anchor.
3. Deliverable: sprite-id scheme + placement rule (tile → world/screen
   anchor, draw layer relative to units/buildings).

### WP3 — RE: auto-bridge on pathway drag (the live applier `0x498a90`)

Substantially advanced (see `exe_trail_re_findings.md` §0 corrections
13-15): the MakePathwayCommand path is DEAD CODE; the live pathway applier
is the giant function at `0x498a90`. Its water-crossing block
(`0x499ecc`-`0x499f73`) already gives the bridge byte semantics:
**byte 4 = averaged per-tile depth from the map layer at `[map+0x2c]`**,
**byte 5 = `0x10 << cardinal_index`** (table `0x5fea00`). The hash-write
`0x4637d0` itself spawns/removes bridge visuals: when a written mask has
byte4 != 0xff it calls into the marker manager (`0x53de70`/`0x53ddd0`,
after `0x463a00`-area blocks) — trace those two callees for BridgeMarker
spawn/removal. Remaining questions:
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

### WP4 — Extractor: export bridge sprites

Add `terr/brid` + `terr/rbrd` item export to
`OpenTE/tools/extractor/sprites/terrain.py` once WP2 fixes the id scheme.
Straightforward `bg6a` leaf → PNG, same as decorations.

### WP5 — Game: draw bridges

Render pass for bridge sprites at bridge tiles (authored now, player-built
after WP6). Placement/variant selection per WP2; treat like decorations
(camera-transformed sprite at tile anchor, z-sorted with the entity layer).

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
