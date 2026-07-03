# Fable notes 1 — response to FableBrief1.md

*2026-07-02. Companion to the session that fixed canal/rail rendering.
Part 1 is what was done; Part 2 is the diagnosis you asked for; Part 3 is
recommendations, most of which are already applied.*

> **Addendum (round 3, same session):** the byte-2/byte-3 attribution is
> now proven from code alone — `MakePathwayCommand::Execute` maps network
> type N to mask byte N, and the RTTI-named `RailFinderIterator` tests
> mask byte 2 (cardinals only) while `CanalFinderIterator` steps all 8
> directions (`exe_trail_re_findings.md` §0 correction 13). Decoding
> Execute also proved diagonal canals are two-tile-wide BRAIDS (the
> flanking tiles carry corner channel pieces — now implemented in the
> preview), and the diagonal-rail pixel offset traced to OpenTE's decal
> UV insets, which the original doesn't have (now EXE-exact). The lesson
> for Part 2: the definitive anchor was 3 tool calls away in the vtable
> map the whole time — semantically-named code consumers beat art or
> observation for attribution questions, and CLAUDE.md gotcha 12 now says
> exactly that.

## 1. What was fixed this session

**Canal & rail rendering — root cause found and fixed (in two steps).**
Step 1 decoded the engine's atlas loader (`0x466790`): dividing its
descriptor offsets by the 28-byte page-table stride gives the complete
node↔page-slot map — `terr/rail/*` fills page slot 0xf6 (drawn by the
mask-byte-2 network) and `terr/cana/*` fills slot 0xfa (byte-3 network).
Every prior "the pipeline is verified" claim had checked only that the
extractor reads the same *node* the game loads, never which *slot* the
node lands in. Step 1's first interpretation ("the container tags are
swapped") kept the inherited byte names (byte2="canal", byte3="rail") and
was **falsified by your in-game test** — which is exactly the
consumer-side ground truth that settles it: **the first-pass byte names
were reversed. Mask byte 2 is RAIL and byte 3 is CANAL; the container
tags were honest all along.** Everything then snaps into place with no
compensating errors anywhere: the 41-code accumulated-LUT network (byte
2) is rail — cardinal-only like all land paths, its 26 "mixed" codes are
road-over-rail level crossings, its cells 37–40 buffer stops; the
33-code network (byte 3) is canal — the game's only 8-directional
network. Canals are ancient-era tech (barges), railroads industrial-era,
matching. Full write-up: `documentation/extracted/path-rendering-handoff.md` §0.

**Diagonal canals decoded and implemented.** The byte-3 key encoding is
no longer opaque: a diagonal canal connection is its diagonal bit plus
both flanking cardinal bits (NE = `0x0e`, SE = `0x38`, SW = `0xe0`,
NW = `0x83`); the 33 valid keys decompose exactly into endpoints,
straights (including the two diagonal straights), all two-connection
bends, tees, and the cross. The construction preview now steps canals
8-connected and emits this encoding, so a diagonal canal drag draws true
diagonal channel cells instead of a zigzag. Trails, roads, and rails
remain cardinal staircases (the EXE's decal builder never reads their
diagonal bits — a diagonal rail is *supposed* to alternate corner
pieces).

**Canal sea-mouths implemented.** The "water bridge" stub path from the
earlier RE turns out to fire only for the 8 single-connection endpoint
keys: it's a canal *ending* at shore-water, drawing sea-mouth cells
45–52 (which is why exactly those cells are populated in the canal
atlas). Implemented as `kCanalMouthLUT`.

**Bridges — ground truth decoded, suppression implemented.**
- `mapp.brid` records **overwrite** connectivity bytes 4/5 (the prior RE
  said "OR", which — against the 0xff default — made every authored bridge
  a no-op; the extractor had faithfully reproduced the bug and silently
  erased bridge data from all 8 maps that ship with bridges). Fixed;
  bridges now survive extraction.
- A bridge tile (`bridge != 0xff`) **suppresses** the tile's network decal
  (the EXE's "bridge path" jumps to the decal setter with page 0). The
  bridge *visual* is a separate sprite (`terr_brid_*` road bridges,
  `terr_rbrd_*` rail bridges), drawn elsewhere. Suppression is
  implemented; the sprite pass and construction auto-bridging are
  packaged as six work packages with addresses, methods, and validation
  data in `OpenTE/implementation/bridge-plan.md` — sized for a smaller
  agent to execute one at a time. The `BridgeMarker` RTTI class is the
  entry point.

**Code quality in the touched area.** `TileConnectivity`'s fields were
named per the *first, wrong* RE pass (`road` held the trail byte,
`trail_extra` the road byte, `canal_dir` was actually rail, `rail`
actually canal, and `canal` the bridge flag), with compensating
NOTE-comments at use sites explaining the mismatches. Renamed to ground
truth (`trail`/`road`/`rail`/`canal`/`bridge`/`bridge_aux`) across game,
extractor, and spec; the compensating comments are gone.
`spec/world-and-maps.md`'s connectivity section (which described a
nonexistent `deep_water` byte and a superseded JSON schema) was rewritten
to match the decoded format and the actual implementation.

## 2. Diagnosis — why agents guess instead of digging

I read the repo, the RE docs, the handoff, the session index (59
TradeEmpires sessions: 54 on Sonnet, mostly 1–2 completed turns each), and
did the dig myself. The knowledge base is genuinely excellent — the fix
above was reachable in about an hour *because* the previous sessions left
`exe_net_loader_466790.txt`, the occupancy measurements, and a ranked-leads
handoff. The problem is not missing information. It's four structural
pressures:

**(a) Session economics select for guessing.** An EXE dive is 20–40 tool
calls with zero user-visible progress until the end; a plausible-looking
estimate produces a diff in three. In a 1–2-turn session on a smaller
model, the guess wins almost every time. The near-misses in the record are
striking: the previous session's Lead B was *the right question* and even
quoted the loader's `lea` offsets — it just misattributed one of them
(0x1b3c = `tran`, not `coa0`) and stopped one arithmetic step short.
That's not a knowledge problem; it's a stamina/verification-budget
problem, and it correlates with model tier and turn count.

**(b) "Verified" gets declared at the producer, not the consumer.** The
canal/rail bug survived a session whose findings doc literally said
"every mechanical stage is identical to trail; any remaining discrepancy
is NOT in the pipeline" — because verification stopped at "the game loads
the same node we extract" without decoding what slot the loader puts it
in. Worse, the smoking-gun section admitted its own theory implied *the
original game would also mis-sample* — an impossibility that should have
been treated as proof of a missing stage, not as a mystery. Both lessons
are now standing rules in CLAUDE.md.

**(c) Errors in the record persist because corrections are appended, not
applied.** Round 23's "brid ORs into the mask" misreading flowed into the
extractor as a faithful implementation of a bug. The findings doc's §3/§7
tables still carry first-pass names that §0 corrects — a reader who lands
mid-document inherits the stale version. Wrong field names flowed into
struct fields, then got *compensated* by comments instead of renamed —
which is exactly the "persisting fixations" smell you noticed. Where
feasible, correct in place (I've annotated the findings doc and corrected
Round 23 inline); when a name is wrong, rename it — never comment around
it.

**(d) Retrieval cost: the docs are a superb archive and a poor index.**
`00-roadmap.md`'s completed-work table has single *cells* of 500+ words;
CLAUDE.md is a flat, monotonically growing gotcha list; and truth about
any one system may live in up to four places (RE docs, `OpenTE/spec/`,
`implementation/*-plan.md`, `extracted/*-handoff.md`) that drift apart —
`spec/world-and-maps.md` contradicted the implementation until today.
Meanwhile the `OpenTE/docs/` per-system doc set promised by
clone-architecture.md doesn't exist. A fresh 1-turn agent reads a fraction
of this and anchors on whichever version it hit first.

On the **image-interpretation habit**: agents reach for vision because it
feels like the cheapest probe, and nothing in the repo said not to. The
project already owns the better pattern — quantitative pixel checks
(occupancy grids resolved this bug; brute-force correlation decoded the
price fields). That's now an explicit CLAUDE.md rule: model-eyes for
*measuring*, user-eyes for *confirming*.

## 3. Recommendations

Applied this session:
1. CLAUDE.md: consumer-side verification rule (container gotcha 12), the
   "if your theory implies the original game is broken, your theory is
   broken" rule, and the no-eyeballing-sprites policy.
2. Corrections applied *in place* (Round 23, findings §0 additions +
   naming banner, handoff §0 resolution, spec rewrite) rather than only
   appended.
3. Ground-truth field names in code; compensating comments deleted.
4. `bridge-plan.md` written as small, independently completable work
   packages with addresses and validation data — the shape of task that
   lower-tier agents complete well.

Suggested going forward:
1. **Match model tier to task type.** Reserve EXE/RE investigations and
   "why does X still look wrong" debugging for the strongest model tier
   and let those sessions run long; hand Sonnet the mechanical work the RE
   output enables (port this decoded table, wire this extractor field,
   apply this plan phase). The session history reads as the mirror image
   of that split.
2. **Keep the handoff-doc discipline** — it's the single best artifact in
   the repo for cross-session continuity (this fix is downstream of it).
   Require one for any investigation that ends unresolved: symptom →
   confirmed algorithm → ruled-out (with method) → the contradiction →
   ranked leads.
3. **Trim `00-roadmap.md` rows to ~3 sentences + a link**; the Stage-D row
   was a doc-in-a-cell. (Partially done for that row.)
4. **Declare one canonical home per fact** and mark completed plan docs as
   historical at the top (done for trail-rendering-plan.md). The spec
   should be updated in the same change that lands an implementation, or
   the deviation logged in spec-deviations.md — never silently diverge.
5. **Commit more often.** The previous session's road/trail fixes were
   sitting uncommitted alongside today's work; fine-grained history is
   also archaeology for future agents.
6. When you next run the game: canals and railroads should draw their own
   art with correct tiles; a diagonal canal drag should produce smooth
   diagonal channel cells; a canal drag ending next to sea should show a
   sea-mouth piece; a diagonal railroad remains an alternating-corner
   staircase (that is the original's behavior — byte-2 networks have no
   diagonal cells). Worth a screenshot pass over ep08_anti / ep12_hang
   (authored bridges + pre-built roads).
