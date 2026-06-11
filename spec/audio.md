# Audio

This document specifies the clone's audio: sound effects, music, and the
events that trigger them. The original uses Miles Sound System
(`mss32.dll`) to play MP3-framed audio from `Data/musi.{}` (20.3MB) and
`Data/soun.{}` (10.5MB) (`documentation/04-other-formats.md`); per-track
offsets within these containers were not enumerated. The clone uses **SDL2_mixer**
(add to `vcpkg.json`) and does not depend on extracting the original's
audio container internals beyond slicing out individual track files.

## Extraction scope

- `Data/soun.{}` and `Data/musi.{}` both begin with valid MPEG audio frame
  sync bytes — i.e. they're containers wrapping individual MP3 (and
  possibly WAV) streams using the same `.{}` directory structure as
  everything else. The extractor should:
  1. Walk the container tree (reusing
     `tools/extractor/containers/container.py`) to enumerate leaves.
  2. For each leaf, write the raw bytes to `game_data/audio/<path>.mp3` (or
     `.wav` if the leaf's bytes don't start with an MP3 frame sync —
     sniff the first few bytes rather than assuming).
  3. Record each in `manifest.json`'s `audio` index (`{id, path, kind:
     "sfx"|"music"}` — `kind` inferred from which root container/sub-tree
     it came from: `soun` -> sfx, `musi` -> music).
- This is a **mechanical, low-risk extraction** (no format decoding beyond
  "find the leaves and write the bytes") — appropriate as a follow-up to the
  spike, not blocking it.

## Playback

```cpp
namespace opente::audio {

class AudioSystem {
public:
    void play_sfx(std::string_view sound_id, float volume = 1.0f);
    void play_music(std::string_view track_id, bool loop = true);
    void stop_music();
    void set_master_volume(float v);
    void set_sfx_volume(float v);
    void set_music_volume(float v);
};

} // namespace opente::audio
```

- SFX use `Mix_Chunk` (`Mix_LoadWAV` works for MP3 too via SDL_mixer's MP3
  support, or pre-transcode to OGG/WAV at extraction time if MP3 chunk
  playback proves unreliable — extraction-time transcoding is acceptable
  since the extractor already has to touch every byte).
- Music uses `Mix_Music` (streamed, one track at a time, with crossfade via
  `Mix_FadeOutMusic`/`Mix_FadeInMusic` on track changes).
- Volume sliders (master/sfx/music) are stored in user settings (not
  `game_data/` — this is player preference, lives alongside save games).

## Sound cue triggers

Cross-referencing identified in `documentation/03-exe-analysis.md`:

| Event | Original cue (decoded 4cc tags) | Clone trigger |
|---|---|---|
| Treasury debited (building/pathway purchase, by local human player only) | `'spnd'`/`'inte'`/`'soun'` ("ka-ching") | `AudioSystem::play_sfx("ui.purchase")` on successful `PlaceBuildingCommand`/`BuildPathwayCommand` for the human player ([input.md](input.md), Round 17) |
| Demand growing for a commodity | `'soun'`/`'evnt'` notification (Round 4-7) | `AudioSystem::play_sfx("market.demand_up")` on `MarketEvent::DemandGrowing` ([simulation.md](simulation.md)) |
| Demand declining | (symmetric) | `AudioSystem::play_sfx("market.demand_down")` |
| Building construction complete | toast message (Round 17 step 6) | `AudioSystem::play_sfx("ui.build_complete")` alongside the toast ([ui.md](ui.md)) |
| Placement invalid (click while `PlacementError != Ok`) | not identified in RE notes | `AudioSystem::play_sfx("ui.error")` — clean addition for UX clarity |

Additional ambient/UI sounds (button clicks, unit movement) are clean
additions, not RE-derived — add as needed, registered the same way (one
`play_sfx(id)` call at the relevant trigger site).

## Music

- One looping background track per episode/region (`episodes.json`'s
  `music_track` field — extractor-derived from the episode's culture set,
  if a per-culture music mapping is found during extraction; otherwise a
  single global rotation of all extracted music tracks is an acceptable
  fallback).
- Crossfade (1-2s) on episode/region change.

## Open questions / RE gaps

- **Per-track boundaries within `musi.{}`/`soun.{}`**: not enumerated yet —
  the extraction approach above (walk the `.{}` tree, slice leaves) should
  work mechanically since the container format itself is fully decoded, but
  hasn't been run against these specific files. If leaves turn out to be
  larger multi-track blobs rather than one-track-per-leaf, additional
  frame-boundary scanning (find consecutive MP3 frame headers) would be
  needed — flag this as a follow-up if the naive per-leaf extraction
  produces obviously-too-large or multi-song files.
- **Episode -> music track mapping**: not identified; clone may need its
  own simple mapping/rotation as noted above.
