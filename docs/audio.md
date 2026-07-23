# Audio

Audio uses SoLoud (miniaudio backend). Packages supply raw clips under sound/ and optional audio definitions under audio/. Playback goes through Scheme, .spanim frame sounds, or flecs AudioSource components.

Related: [Package structure](package-structure.md), [Sprites](sprites.md) (frame sounds), [Scripting](scripting.md), [Player](player.md).

## Mental model

Raw clips are .ogg files under sound/ -- one-shot SFX, music streams, and .spanim frame sounds. Audio defs under audio/ are a second layer: procedural Sfxr (.saudio) or sample wrappers (.s7) that point at a clip and carry playback options. Mixing goes through two buses, sfx and music. Playback reaches those layers from Scheme (play-sound / play-audio and friends), from .spanim (sound ...) on hold enter, or from flecs AudioSource autoplay.

## Steam Audio (optional)

By default, spatial playback uses SoLoud's built-in 3D path. Building with Steam Audio replaces that backend for spatial sources.

The Steam Audio SDK is an **out-of-repo binary**: it is not a submodule and is not under lib/. Each machine that opts in supplies STEAM_AUDIO_ROOT at configure time (see the [README](../README.md#optional-steam-audio) for CMake flags and expected SDK layout).

When linked and initialized successfully:

- Spatial voices use an HRTF spatialize filter
- Map load builds an occlusion/transmission scene from VIS visible faces
- Parametric reflections run through the simulator
- BUILD_RPATH / INSTALL_RPATH point at the SDK lib dir so libphonon resolves at run time

If init fails, the engine logs a warning and falls back to SoLoud 3D. Authoring is unchanged: (spatial #t) on defs, sound-source things, .spanim frame sounds, and AudioSource.spatial all use Steam when it is enabled.

## Package layout

| Kind | Directory | Extensions | Virtual path example |
|------|-----------|------------|----------------------|
| Sound | sound/ | .ogg | weapons/fire -> sound/weapons/fire.ogg |
| Audio def | audio/ | .saudio, then .s7 | ui/pickup -> audio/ui/pickup.saudio |

Author raw clips as .ogg. Procedural and sample-wrapper defs resolve .saudio then .s7. Virtual paths omit the directory and extension.

play-sound, play-music, frame (sound ...), and AudioSource.clip take raw paths under sound/. play-audio and AudioSource.audio take defs under audio/.

## Raw sounds

Place .ogg files under sound/. Example: sound/weapons/fire.ogg -> virtual path weapons/fire.

```text
(play-sound "weapons/fire")
(play-sound "weapons/fire" 0.8)
(play-sound "ambience/hum" 0.5 #t)
(play-music "music/theme" 0.5)
```

play-music streams a raw clip on the music bus, always loops, and replaces any previous music voice.

## Audio definitions

### Procedural (.saudio)

Sfxr-style synth under (audio ...):

```text
(audio
  (preset coin)
  (volume 0.8)
  (bus "sfx"))
```

Or with a seed and wave overrides:

```text
(audio
  (preset laser 42)
  (wave square)
  (freq 0.4)
  (attack 0.0)
  (sustain 0.2)
  (decay 0.3)
  (volume 1.0)
  (spatial #f)
  (bus "sfx")
  (filter "echo"))
```

| Field | Meaning |
|-------|---------|
| (preset name [seed]) | coin, laser, explosion, powerup, hurt, jump, blip |
| (wave type) | square, saw, sine, noise |
| Synth params | freq, freq-limit, freq-ramp, freq-dramp, duty, duty-ramp, vib-strength, vib-speed, vib-delay, attack, sustain, decay, punch, lpf, lpf-resonance, lpf-ramp, hpf, hpf-ramp, pha-offset, pha-ramp, repeat-speed, arp-speed, arp-mod, sound-vol, master-vol |
| (volume n) | Playback gain |
| (loop #t\|#f) | Loop when played as a def |
| (spatial #t\|#f) | 3D when played from a positioned source |
| (min-distance n) / (max-distance n) | Attenuation range |
| (bus "sfx"\|"music") | Target bus (music replaces the music voice) |
| (filter "name") | Builtin filter to apply (repeatable) |

### Sample wrappers (.s7)

Evaluated only while an audio def is loading. Macros (audio ...) and (register-audio ...) register a sample def. (source "...") is required (virtual path under sound/).

```text
(audio
  (source "weapons/fire")
  (volume 1.0)
  (loop #f)
  (spatial #f)
  (bus "sfx"))
```

Same playback fields as .saudio (volume, loop, spatial, distances, bus, filter).

## Scheme API

| Binding | Signature | Returns | Notes |
|---------|-----------|---------|-------|
| play-audio | (play-audio path [volume]) | voice handle (int), 0 on fail | Loads def from audio/ |
| play-sound | (play-sound path [volume] [loop?]) | voice handle | Raw clip on sfx bus |
| stop-sound | (stop-sound handle) | #t / #f | |
| set-sound-volume | (set-sound-volume handle vol) | #t / #f | |
| set-bus-volume | (set-bus-volume bus vol) | #t / #f | bus is "sfx" or "music" |
| play-music | (play-music path [volume]) | #t / #f | Streams raw clip; loops; stops previous |
| stop-music | (stop-music) | #t / #f | |
| audio-filter-attach | (audio-filter-attach target filter [slot]) | #t / #f | target: "sfx", "music", or "global" |
| register-audio-filter | (register-audio-filter name proc) | #t / #f | Stores a Scheme proc (not applied at playback yet) |

Load-time macros (only while evaluating an audio/ .s7 def): audio, register-audio.

## Buses and filters

Buses are created at audio init. Music uses a streaming voice on the music bus; SFX uses cached sample / Sfxr voices on the sfx bus.

Builtin filter names:

echo, biquad, lofi, flanger, bassboost, freeverb, robotize, waveshaper, dcremoval

Attach at runtime:

```text
(audio-filter-attach "sfx" "echo")
(audio-filter-attach "music" "bassboost" 0)
(audio-filter-attach "global" "dcremoval")
```

Per-def (filter "...") lists apply when that def plays.

## Frame sounds

In .spanim, a hold may include (sound "path" [volume]). On hold enter, the animator plays the raw sound/ clip (not an audio def). World entities with a global transform get 3D playback. See [Sprites: Frame sounds](sprites.md#frame-sounds). Sibling (hint "name") annotations are not audio; they call Scheme (on-sprite-hint ...) on the same hold enter — see [Sprites: Logic hints](sprites.md#logic-hints).

```text
(frame "Fire" 0.08 (sound "weapons/fire" 0.9))
```

[slopsprite](slopsprite.md) can pick paths from mounted sound/ folders into this field.

## Entity components

No Scheme bindings for these today; useful for C++ / systems authors.

| Component | Role |
|-----------|------|
| AudioListener | Active listener; orientation syncs with first-person yaw/pitch when present |
| AudioSource | audio (def path) and/or clip (raw path); volume, distances, looping, spatial, autoplay, voice handle |

Autoplay starts on enable; sources stop on remove. A sync system updates listener and 3D source positions each frame.

## Recipes

Weapon fire from a clip:

```text
;; sprites/fp/weapons/gun.spanim
(frame "Fire" 0.05 (sound "weapons/fire"))
```

UI blip from a procedural def:

```text
;; audio/ui/pickup.saudio
(audio
  (preset coin)
  (volume 0.7)
  (bus "sfx"))

;; Scheme
(play-audio "ui/pickup")
```

Background music:

```text
(play-music "music/theme" 0.5)
(set-bus-volume "music" 0.4)
```
