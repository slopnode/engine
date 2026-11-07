@page cli Command line arguments

`slopengine` and the `slop*` tools share one small argument parser. It understands a fixed set of "mount" flags for locating packages, and hands anything it doesn't recognize to the base package, which can declare its own flags in `data/cli.s7`.

# Mount flags {#mount-flags}

```
slopengine --game <path|name> [--mod <path|name>]... [--profile <name>] [--debug] [package-flags...]
```

- `--game` -- the game package to run. Either a directory containing a `package.meta`, or a name looked up in the configured search paths. Required.
- `--mod` -- an additional package mounted on top of the game, same lookup rules as `--game`. Repeatable; mods apply in the order given, later mods overriding earlier ones for the same asset path.
- `--profile` -- selects which settings/saves/screenshots directory tree to use. Defaults to `"default"`. See @ref profiles for what this scopes.
- `--debug` -- enables the developer-only UI (the Debug menu, entity inspector, and related overlays). Off by default so a normal player never sees it.
- `--perf-log` -- logs the Performance window's numbers to the trace log every two seconds, averaged over the interval: CPU and GPU pass timings on one line, draw, cull, ray and probe counters on the next.
- `--perf-view x,y,z,yaw,pitch` -- locks the view to the debug free camera at that pose every frame, so two runs measure the same picture whatever happens at the keyboard. Yaw and pitch are radians.
- `--listen port` -- hosts a game on `port` or `host:port`. Each client drives a player body of its own, and on a dedicated server the first one drives the map's player. Use it with `--dedicated` for a server without a window. The server keeps its encryption key in `server.key` in the profile directory and logs the key's fingerprint at startup. See @ref networking_design.
- `--connect host:port` -- joins a game. The client loads whichever map the server names, sends its input, and shows what the server sends. With `--dedicated` it joins without a window, which is useful for testing.
- `--server-key key` -- with `--connect`, refuses a server whose key isn't this 64-digit hex key. Without it the client remembers each server's key in `known_servers` in the profile directory on the first visit and refuses that address if its key later changes.
- `--session kind` -- with `--listen`, hosts a session of this kind from the game's `*net-sessions*`. Without it the server hosts the first kind declared. The kind's `players` maximum caps how many players can be in the game at once, the host included.
- `--max-players n` -- with `--listen`, lowers the cap below the session kind's maximum. It can't raise it, and scripts can't raise the cap past it either. A game that declares no session kinds allows 8 players, and this flag sets that number.
- `--dedicated` -- runs only the simulation, at 60 ticks per second, until interrupted with Ctrl-C or SIGTERM. No audio device opens, nothing is drawn, the local input device is ignored, and maps load without a loading screen. Pick the map with the base package's own flag, for example `--map test` for the bundled games. The engine still opens a hidden window, because loading a map uploads meshes and textures and needs a graphics context, so a display is still required. The map's player entity is created as usual and stands idle.

Where a `--game`/`--mod` value resolves to on disk is covered in @ref packages, since it's the same search path logic package dependency resolution uses.

Running with no arguments, or an unrecognized flag before `--game` is satisfied, prints usage and exits:

```
Usage: slopengine --game <path|name> [--mod <path|name>]... [--profile <name>] [package-flags...]

  --game        Game package: a directory path, or a name looked up in
                the configured search paths (required)
  --mod         Additional mod package: path or name, same lookup as
                --game (repeatable)
  --profile     Settings/saves/screenshots profile to use (default: "default")
  --debug       Enable developer-only UI (e.g. the Debug menu)
  --verbose     Raise the trace log level to show DEBUG messages
  --perf-log    Log averaged frame timings and counters every two seconds
  --perf-view   Lock the camera at x,y,z,yaw,pitch for repeatable measurements
  --dedicated   Run the simulation only, in a hidden window with no audio,
                drawing or local input
  --listen      Host a game on port or host:port
  --connect     Join a game at host:port, or with --dedicated join without a window
  --server-key  With --connect, refuse servers whose key isn't this 64-digit hex key
  --session     With --listen, the session kind from data/net.s7 to host
  --max-players With --listen, allow fewer players than the session kind does
  --version     Print the engine version and exit
```

# Package flags {#package-flags}

A base package can declare its own flags by defining `*package-cli*` in `data/cli.s7`:

<pre><code class="language-scheme">; data/cli.s7
(define *package-cli*
  '((flags
     ((name "map") (value "string") (help "Initial map folder under maps/"))
     ((name "verbose") (value "flag") (help "Enable verbose logging")))))
</code></pre>

`value` is either `"string"` (the flag takes the next argument as its value) or `"flag"` (a boolean switch with no value). Anything on the command line after the mount flags is matched against this schema -- an unknown `--something` or a missing value is a startup error, printed together with the package's own usage list.

Inside scripts, read a flag's value with `startup-arg`:

<pre><code class="language-scheme">; scripts/init.s7
(define (on-startup)
  (let ((map-id (startup-arg "map")))
    (if map-id
        (request-map-load map-id "fresh")
        #t)))
</code></pre>

`startup-arg` returns `#f` when the flag wasn't passed. For a `"string"` flag it returns the value as given on the command line. For a `"flag"` switch it returns the string `"#t"`, not the boolean `#t` -- a plain `(if (startup-arg "verbose") ...)` still works since any non-`#f` value is truthy in Scheme, but `(equal? (startup-arg "verbose") #t)` will not.

Note that this schema is only known once the base package's own scripts have loaded, so package flags are parsed after `--game` is mounted -- a package flag typo doesn't prevent the mount flags themselves from being validated, but it does stop the game from starting.

# Tool binaries {#tool-binaries}

`slopcsg`, `slopbsp`, `slopvis`, `slopnav`, and `sloprad` accept the same `--game [--mod]...` mount flags plus a required `--map <name>` identifying which map to process (`slopnav` also takes an optional `--compare` diagnostic flag, and `slopvis` an optional `--mode full|fast`, where fast stops after the plane-test base pass, looser but seconds instead of minutes on large open maps, and `--fast` is a shorthand for `--mode fast`):

```
slopcsg --game path/to/game --map my-room
slopbsp --game path/to/game --map my-room
slopvis --game path/to/game --map my-room
slopnav --game path/to/game --map my-room
sloprad --game path/to/game --map my-room
```

`slopvis --audit <pairs>` checks an existing vis file instead of building one: it samples pairs of interior leaves, marches a segment between them through the tree and reports pairs that are open yet marked invisible, plus portal pairs blind to each other and area gate mismatches. `slopvis --audit-view x,y,z` does the same for one camera point against every surface face. In the engine, Debug → "Cull map meshes by PVS" switches the world cull off live, and running with `SLOPENGINE_CULL_AUDIT=1` logs any culled map mesh that a march from the camera still reaches.

That's the required run order, CSG → BSP → VIS → NAV → RAD (see @ref tut_csg): `slopcsg` carves overlapping brush faces (see @ref carving-resolving-overlapping-brushes) and writes the result `slopbsp` reads for surface geometry, so `slopbsp` fails with "run slopcsg first" if that carved output is missing or stale; `slopnav` and `sloprad` both only need `slopbsp`'s output, but are conventionally run in that order after `slopvis`.

`slopmap`, `slopsprite`, `slopthing`, `slopaudio`, `slopmat`, and `slopmodel` parse their own arguments (`--target`, an optional `--map`, and so on) rather than going through the package-flag schema, since they're editors rather than something a base package configures. Each takes `--game` plus an optional `--target` naming the package that receives saves; `--target` defaults to `--game` and is only required once `--mod` is also given. See their individual pages under @ref tools for the flags each one takes.
