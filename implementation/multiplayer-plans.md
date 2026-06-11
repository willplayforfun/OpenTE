# Multiplayer Plans

This document sketches a design for a **novel multiplayer mode** for OpenTE —
something the original game never had. It is **not** part of the base spec
([`OpenTE/spec/`](../spec/)) and does not block or gate the single-player
implementation roadmap ([roadmap.md](roadmap.md)). Treat this as a forward-
looking design sketch to revisit once the base game is playable; expect it to
change as the single-player architecture solidifies.

**Status: not started. Do not begin implementation until the base game
(roadmap.md Stages 1-8) is complete and playable single-player.** Multiplayer
adds a parallel client/server split to every system in the spec — building it
against a moving single-player target would mean re-doing this work
repeatedly.

## Guiding architectural choice: authoritative server, no lockstep

The spec's existing choices (`float`/`double` tile coordinates, modern PRNGs
seeded per-game — see [world-and-maps.md](../spec/world-and-maps.md) "Open
questions" and [simulation.md](../spec/simulation.md)'s RNG notes) are
incompatible with bit-exact lockstep determinism across machines. Rather than
fight this, multiplayer uses a **client-server model**:

- One peer (the **host**) — or a separate dedicated process — runs the
  simulation exactly as specified in [simulation.md](../spec/simulation.md)
  and [entities.md](../spec/entities.md), unmodified from the single-player
  code path.
- Clients send **commands** (`PlaceBuildingCommand`, `BuildPathwayCommand`,
  `IssueTradeOrderCommand`, `ResearchCommand`, `SetSpeedVoteCommand`,
  `ChatMessageCommand`, ...). These map directly onto the command types
  already implied by [input.md](../spec/input.md) (placement/pathway
  commands) and [simulation.md](../spec/simulation.md) (research, trade
  orders).
- The host validates each command against the same legality checks used
  single-player (e.g. [input.md](../spec/input.md)'s `PlacementError`
  checks), applies it, and the simulation advances normally.
- The host periodically broadcasts **state snapshots** (full or delta) of
  changed entities/`MarketGood`s. Clients never run their own copy of the
  simulation — they only hold a "received state" view that the renderer
  consumes, identically to how single-player rendering consumes the local
  simulation state ([overview.md](../spec/overview.md)'s `alpha`-interpolated
  render step works unchanged either way).

**Key simplification: no client-side prediction, no rollback.** The local
player's commands are sent to the host and only take effect once
acknowledged/applied there. The only "local-only" UI state is placement
*preview* (highlight overlays, [input.md](../spec/input.md)), which was
already client-side-only and doesn't touch simulation state. This avoids the
single biggest source of complexity in real-time-game netcode (rollback/
reconciliation) at the cost of a small input-to-effect delay (one network
round trip), which is acceptable for a city-builder/economy game's pacing.

This means **the simulation/rendering split already specified in
[overview.md](../spec/overview.md) is the multiplayer split**: single-player
is "renderer reads local simulation state every tick"; multiplayer is
"renderer reads simulation state received from the host every snapshot." The
renderer, UI, and input-handling code should not need to know which mode
they're in.

## Command queue architecture (shared single-player/multiplayer)

Even single-player should route player actions through the same command
types (`PlaceBuildingCommand`, etc.) applied to a local queue each tick. This
isn't extra work *for* multiplayer — it's the natural way to implement
"validate then apply" — but it means multiplayer support is additive: the
network layer just becomes another producer of commands (remote players' →
host's queue) and another consumer of resulting state (host's snapshots →
clients' render state).

```
Single-player:  Input -> CommandQueue -> Simulation -> RenderState -> Renderer
Multiplayer
  (client):     Input -> [send to host] ......................... [recv snapshot] -> RenderState -> Renderer
  (host):       Input -> CommandQueue -+                                |
                 [recv from clients] --+-> Simulation -> RenderState ---+--> [broadcast snapshot]
                                                       -> Renderer (host's own view)
```

## Snapshot format & the JSON-save translation layer

[world-and-maps.md "Save format"](../spec/world-and-maps.md) already defines
a JSON schema covering everything a snapshot needs (entities, `MarketGood`
state, per-player treasury/tech, sim clock). Reusing it directly:

- **Full snapshot** (on join/reconnect/host-migration handoff) = the save
  JSON, sent once. This is a big win — "join a game" and "load a save" become
  the same code path.
- **Delta snapshots** (steady-state, every N ticks) need a **compact binary
  format**, since JSON is too heavy for 10-20Hz updates. Plan: a translation
  layer that maps the same logical schema (entity fields, `MarketGood`
  fields) to a dense binary delta encoding (changed-field bitmask + packed
  values per changed entity). This layer sits *between* the simulation's
  in-memory state and the network — it doesn't change the JSON save format or
  the in-memory `Entity`/`BuildingState`/`MarketGood` structs from
  [entities.md](../spec/entities.md) at all.
- Keeping the binary format schema-derived (not hand-maintained) — e.g.
  generated from the same struct definitions used for JSON
  serialization — avoids the two formats drifting apart.

## Networking transport

- **Library**: ENet (UDP, reliable + unreliable channels, vcpkg-available —
  fits the existing CMake/vcpkg toolchain). Reliable channel for commands/
  chat/full snapshots; unreliable (or unreliable-sequenced) channel for delta
  snapshots, where a dropped update is superseded by the next one anyway.
- **Transport abstraction**: define a `Transport` interface
  (`send(peer_id, bytes)` / `poll() -> messages`) with at least two
  implementations:
  1. **Direct UDP** (post-NAT-punchthrough socket).
  2. **Relayed** (forwarded through the master server or a relay process).

  The simulation/command/snapshot layer talks only to `Transport` — it never
  knows whether a given peer connection is direct or relayed. This is what
  keeps **host migration** viable later (see below) without redesigning the
  protocol.

## NAT traversal: punchthrough + relay fallback

- **Primary: UDP hole-punching**, brokered by the master server (STUN-like
  signaling: each peer reports its observed public `IP:port` to the master
  server, which forwards candidates to the other peer; both sides send
  packets to punch their NATs). Works for the common home-router NAT types
  (full-cone, restricted-cone, port-restricted-cone).
- **Fallback: relay**, for symmetric NAT / CGNAT pairs where punchthrough
  can't succeed (mobile/cellular networks and some ISPs' carrier-grade NAT
  are common cases). If direct UDP doesn't establish within a timeout, fall
  back to relaying packets through the master server (or a dedicated relay
  process) — this is the standard STUN+TURN pattern.
- Relay is **bandwidth-proportional to game traffic** (which is small for
  this genre — snapshot deltas, not an FPS-tier data rate), but still real
  cost for whoever runs the relay. Make it **opt-in/configurable per
  self-hosted master server instance** (off by default, with a bandwidth
  warning), while official/public instances may enable it.

## Master server (self-hostable)

Ship a small, separate **master server executable** alongside the game,
combining two logically-separate responsibilities in one binary:

1. **Lobby registry**: hosts register/heartbeat
   (`name`, `episode`, `player count`, connection info); clients query the
   list. Stateless-ish, tiny (in-memory or SQLite).
2. **Connection signaling**: brokers NAT-punchthrough candidate exchange
   (and optionally relays packets, per above) for any connection attempt,
   whether or not it came from the lobby list (e.g. direct-connect-by-
   hostname also goes through signaling for punchthrough help).

Players can point their game client at **any hostname** running this
executable — official instance, a friend's self-hosted instance, or a LAN-
local instance. The game ships with a default/official instance address but
this should be a config value, not hardcoded.

Two cheaper discovery paths that need **no infrastructure** and should ship
regardless of master-server status:

- **Direct connect**: host shares `IP:port` out-of-band (voice chat,
  Discord). Still benefits from punchthrough signaling if a master server is
  configured, but can fall back to "both players forward a port manually" if
  not.
- **LAN discovery**: UDP broadcast on the local network, no master server
  needed at all.

## Host migration (design-for-later, not built now)

Not implemented in the first version, but the architecture above shouldn't
preclude it:

- The **command-queue + snapshot model already makes the host mostly
  stateless from clients' perspective** — a client just needs *a* peer to
  send commands to and receive snapshots from. If the host disconnects, any
  client already holds a recent full/delta snapshot and could become the new
  host by resuming the simulation from that state (same code path as "load a
  save" / "client receiving a full snapshot on join", just running the
  simulation loop locally now).
- The `Transport` abstraction (above) means re-establishing connections to a
  new host is "just" another round of signaling — relayed connections in
  particular don't need re-punching, the relay just re-points its forwarding
  table.
- **Open question**: how the remaining peers agree on *who* becomes the new
  host (deterministic rule, e.g. lowest player ID / longest-connected; or a
  re-election via the master server) is unsolved — see Open Questions below.

## Lobby UI

- **Host game**: pick episode/map ([world-and-maps.md](../spec/world-and-maps.md)
  `regions[]` already gives per-map starting regions/players), port, max
  players. Shows connected-player list with ready/not-ready.
- **Join game**: address entry, or pick from the master server's lobby list,
  or LAN-discovered list.
- **Player list panel**: name, civilization/region, ready state, ping. Host
  can kick.
- **Mid-game disconnect**: a leaving non-host player's region/entities pause
  or fall under AI control ([opponent-ai.md](../spec/opponent-ai.md) — no new
  AI design needed, just "this player slot is now AI-controlled"). Host
  leaving: see Host Migration above; until that's built, host leaving ends
  the session (with an autosave).

## In-game text chat

A `ChatMessageCommand{from_player, text, timestamp}`, relayed by the host to
all clients, rendered in a scrollable overlay panel (reuse the toast/
notification stack from [ui.md](../spec/ui.md)). Low effort, build early.

## Discord integration

Two independent, additive pieces (Discord Game SDK, requires a registered
Discord application):

- **Rich Presence**: "Playing OpenTE — Episode 1 China, 3/4 players" on
  players' profiles. Cosmetic only.
- **Ask to Join / invites**: Discord hands the game a join secret on accept,
  which the game turns into a direct-connect (still subject to NAT
  traversal above). Effectively lets Discord serve as ad-hoc matchmaking for
  friend groups with zero extra infrastructure.

## Simulation speed: per-player request, host computes effective speed

Each connected player sends a `SetSpeedVoteCommand{requested_speed}` (one of
`pause`, `0.5x`, `1x`, `2x`, `4x`) whenever changed. The host computes:

```cpp
double effective_speed() {
    if (any_player.requested_speed == Pause) return 0.0;
    return *std::min_element(all players' requested_speeds);
}
```

i.e. the most conservative request wins — "everyone must agree to *at least*
this speed." This plugs into [overview.md](../spec/overview.md)'s accumulator
loop as `SIM_DT / effective_speed`, computed host-side only. The HUD shows
each player's current speed request (small per-player icon row) so it's
visible who's holding back a speed-up. Pause should probably be rate-limited
or shown as a dismissible "Player X paused the game (auto-resumes in Ns)"
toast to discourage griefing.

## Episode selection

**Host selects in the lobby**, before the session starts. Maps to
[world-and-maps.md](../spec/world-and-maps.md)'s `regions[]` (per-map starting
regions/players) — host picks an episode/map with enough regions for the
connected player count, then players pick/are assigned regions. A vote system
is explicitly **not** planned for v1 — adds tally/timeout/tie-break UI for
little benefit in small friend-group sessions. Could revisit if/when a public
matchmaking lobby list (vs. friend groups) becomes common.

## End-game flow

Episode end is already a defined trigger
([simulation.md](../spec/simulation.md) `late` tick). On trigger:

1. Host freezes the simulation, broadcasts final state.
2. All clients show a scoreboard dialog (per-player treasury, buildings,
   tech researched — exact scoring formula is a fresh design, not ported from
   the original).
3. Options for all players: **Return to lobby** (same connections, pick next
   episode — supports a "campaign night" flow) or **Disconnect**.

## Open questions / design decisions to settle later

- **Player-slot ↔ region mapping**: how does the lobby map N connected
  players onto a map's `regions[]` entries
  ([world-and-maps.md](../spec/world-and-maps.md))? Auto-assign vs.
  player-pick, and what happens if there are more regions than players (AI
  fills remaining regions — consistent with single-player, but the UI for
  "claim a region" needs designing).
- **Disconnected/AI-controlled player re-join**: if a player who was
  AI-controlled after disconnect rejoins mid-session, how do they resume
  control gracefully (does the AI finish its current "thought", does the
  player's queued commands get discarded)?
- **Host migration election rule**: which remaining peer becomes host, and
  how is this agreed on (deterministic ordering vs. master-server-brokered
  re-election)? Listed as a known gap above — needs a concrete algorithm
  before host migration can be built.
- **Anti-cheat / trust model**: host is fully authoritative and fully
  trusted in this design — a malicious host can see/alter everything. Is
  this acceptable for the target audience (friend groups), or does a future
  "dedicated server" mode (host = neutral process, no player has elevated
  trust) need to be a first-class option from the start? If so, does that
  change the "host" vs "dedicated server" distinction in the lobby UI now,
  even if dedicated-server hosting isn't built immediately?
- **Fog of war / per-player visibility**: does each player see the full map
  state, or only their own region/network? This significantly affects
  snapshot size and whether the host needs to send *different* snapshots to
  different clients (vs. one broadcast snapshot to all). Not addressed by the
  base spec ([world-and-maps.md](../spec/world-and-maps.md)) — needs a
  decision before snapshot format is finalized.
- **Binary delta-snapshot format specifics**: schema-derived encoding scheme
  (changed-field bitmask design, varint/quantization for floats, entity
  ID stability across snapshots) needs an actual spec once
  [entities.md](../spec/entities.md)'s structs stabilize post-Stage-7.
  Premature to nail down before the structs exist.
- **Reconnect window / timeout policy**: how long does a disconnected
  player's slot stay "reconnectable" (resumable via full-snapshot resync)
  before permanently falling to AI control?
- **Master server protocol versioning**: since master servers are
  independently self-hosted and may run older/newer versions than clients,
  what's the compatibility policy (a `format_version`-style field, similar to
  [data-model.md](../spec/data-model.md)'s `manifest.json` versioning)?
- **Voice chat**: explicitly out of scope (Discord covers this for the target
  audience) — confirm this remains true if a public-matchmaking path (no
  shared Discord server) becomes a goal.
- **Relay bandwidth/abuse limits**: if a self-hosted master server enables
  relay mode, what prevents it from being used as an open relay for
  unrelated traffic (rate limiting, only relay between peers that completed
  signaling through this instance, etc.)?
