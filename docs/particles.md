# Particles

Particle systems are authored as `.prt` files under `particles/`. One file is a multi-emitter system referenced by a virtual path (for example `fx/generic-smoke` → `particles/fx/generic-smoke.prt`). Studio authoring is out of scope; place systems from maps, Scheme, or `.spanim` cues.

## Format

```text
(particle-system
  (duration 0)
  (loop #t)
  (emitter "smoke"
    (sim gpu)
    (sprite "fx/smoke")
    (blend alpha)
    (billboard face)
    (max-particles 256)
    (rate 24)
    (burst 0)
    (lifetime 1.2 2.0)
    (speed 0.1 0.4)
    (size 0.3 0.7)
    (color 1 1 1 0.85)
    (gravity 0.05)
    (space world)
    (shape sphere 0.15)
    (size-over-life 1.0 0.3)
    (alpha-over-life 0.6 0.0))
  (emitter "bits"
    (sim cpu)
    (sprite "fx/smoke")
    (blend additive)
    (max-particles 32)
    (burst 8)
    (rate 0)
    (lifetime 0.4 0.8)
    (speed 1.5 3.0)
    (size 0.05 0.12)
    (gravity 3.0)
    (shape cone 20 0.05)
    (bounce 0.35)
    (max-bounces 2)
    (die-on-hit #f)))
```

| Field | Notes |
|-------|-------|
| duration | System lifetime in seconds. `0` = infinite while playing. |
| loop | When duration elapses, restart emitters if true. |
| sim gpu\|cpu | `gpu` = no world collision (high-count fluff). `cpu` = bounce off brush geo via raycast. |
| sprite | Required `.spr` virtual path. Particles draw as Doom-style billboards. |
| clip | Optional `.spanim` clip name (reserved for frame animation; v1 samples frame A). |
| billboard | `face` / `view` / `fixed` = Y-up cylindrical (Doom actor style). `screen` = full camera-facing (plasma-style). |
| blend | `alpha` (default; premultiplied soft compositing) or `additive` (glow/smoke/sparks). Soft FX sprites should be authored premultiplied (RGB≈A). |
| unlit | When true, skip scene lighting (full authored tint). Default lit: multiply by bake + dyn/FX receiver tint. |
| max-particles | Cap per emitter. |
| rate | Continuous emission per second. |
| burst | One-shot count when the system starts (or restarts). |
| lifetime / speed / size | Constant or `min max` range. |
| color | `r g b a` or eight floats for min/max RGBA. |
| gravity | Y gravity scale. |
| space | `world` or `local` (local transforms spawn offset/direction by emitter pose). |
| shape | `point`, `box w h d`, `sphere r`, `circle r`, `cone angle-deg radius`. |
| size-over-life / alpha-over-life | 2–4 float keys, linear over normalized age. |
| bounce / max-bounces / die-on-hit | CPU only. Visual bounce; no damage or Scheme hit hooks. |

Stock example: `packages/engine/particles/fx/generic-smoke.prt` with sprite `fx/smoke`.

## Runtime

Each placed or spawned system is a flecs entity with `ParticleSystemInstance` plus a transform. Emitters keep SoA particle buffers. CPU emitters raycast against `PhysicsWorld` static geo and reflect; GPU emitters skip collision. Draw is a depth-sorted billboard pass after world sprites (`alpha` and `additive` emitters are batched separately). Lit emitters multiply authored tint by `sampleReceiverTintColor` (bake + dyn/FX) per particle unless `(unlit #t)` or the render unlit debug flag is set.

## Placement

### Map thing

```text
(particle
  (id "vent-smoke")
  (at 0 1 0)
  (yaw 0)
  (system "fx/generic-smoke")
  (play #t))
```

### Scheme

| Form | Role |
|------|------|
| (particle-spawn id x y z path [yaw]) | Spawn a playing system |
| (particle-spawn-fp id socket path [depth]) | Spawn at an FP ViewSprite `(muzzle …)` tip and follow that tip while emitting (`socket` is `"weapon"` / `"emission"`; `depth` is meters along the presentation camera ray, default 0.35) |
| (particle-play id) | Restart and play |
| (particle-stop id) | Stop emission/update |
| (particle-despawn id) | Destroy the entity |

### Animation cue

On `.spanim` hold enter, `(particle "fx/generic-smoke")` or `(particle "fx/generic-smoke" x y [z])` spawns a world-space one-shot system at the host origin plus offset (same hold-enter path as sound / hint / overlay).
