@page profiles Profiles & settings

The engine keeps two different kinds of persistent user data on disk: a global settings file that isn't scoped to any particular game, and a per-profile tree of settings and saves that is. Both live under a per-OS config directory (`src/core/user_paths.cpp`):

- Linux: `$XDG_CONFIG_HOME/slopengine` (falls back to `~/.config/slopengine`)
- Windows: `%APPDATA%\slopengine`
- macOS: `~/Library/Application Support/slopengine`

# Global settings

`<config>/settings.cfg` holds the `[paths]` section only -- additional directories to search when `--base-game`/`--mod` is given a name rather than a path:

```
[paths]
search_path=/home/me/slopengine-games
search_path=/mnt/games/slopengine
```

This file is unscoped by profile or by which package ends up mounted, because it's what makes finding the package possible in the first place -- scoping it by package would make finding the package depend on already having found it. Nothing in the engine writes this file for you; create it by hand if you keep games outside the default search locations. See @ref packages for the full search order these paths participate in.

`<config>/slopmap.cfg` is a separate file for the `slopmap` editor's own settings (window layout, recent files) and isn't part of the profile system below.

# Profiles

`--profile <name>` (default `"default"`) selects which settings file the game reads and writes. What makes a profile a profile is that it's scoped by three things together: the mounted engine package's id and version, the mounted base game's id and version, and the profile name itself:

```
<config>/profiles/<engine-id>_<engine-version>/<base-id>_<base-version>/<profile>/settings.cfg
```

For example, running `slopengine.engine` 0.4.3 against a base game `slopenstein` 0.1.0 with the default profile writes to:

```
<config>/profiles/slopengine.engine_0.4.3/slopenstein_0.1.0/default/settings.cfg
```

The reasoning is in the code comment on `profileSettingsPath`: `"default"` (or any other profile name) should mean something different per game, not one settings file shared across every base game you own. A consequence worth knowing is that it's also scoped by *version* -- upgrading the engine or the base game to a new version starts that profile with a fresh settings file rather than carrying the old one forward. If a profile's settings.cfg doesn't exist yet the first time it's launched, the engine writes a baseline one immediately after startup (graphics defaults plus the full core+package action set), rather than leaving the directory empty until the player opens the settings UI.

A profile's `settings.cfg` has two sections:

```
[graphics]
width=1280
height=720
mode=windowed
vsync=1
dynamic_lights=1
dynamic_light_shadows=1

[controls]
move-forward=W
move-back=S
fire=MOUSE_LEFT
...
```

`mode` is one of `windowed`, `fullscreen`, `borderless`. Unrecognized keys and malformed lines are ignored rather than rejected, so hand-editing a stray typo into this file degrades to "that one setting stays at default" instead of a load failure. Width/height below 640x360 are clamped back up on load.

Screenshots (`<config>/screenshots`) are not currently scoped by profile -- they land in one shared directory regardless of which profile or base game took them.

# Save data

Saves live under `<config>/saves`, scoped by the same `<id>_<version>` segments as settings, but for the whole mounted stack rather than just engine+base:

```
<config>/saves/<engine-id>_<engine-version>/<base-id>_<base-version>/<mods>/
```

`<mods>` is every mounted mod's `<id>_<version>` joined with `+`, in the order given on the command line (not sorted), or the literal string `vanilla` when no mods are mounted. Two consequences follow directly from that: adding, removing, or reordering `--mod` arguments changes which save directory a run reads from, and, as with profile settings, bumping the engine or base game version starts a new save tree rather than reusing the old one.

The first time a given mount combination is used, the engine writes a `mount.s7` sidecar into that directory recording exactly what was mounted (id, version, and root path for the engine, the base, and each mod), so the save tree is self-describing even if you later forget which mod set produced it:

<pre><code class="language-scheme">(mount
  (engine (id "slopengine.engine") (version "0.4.3") (root "/home/me/repos/engine/packages/engine"))
  (base (id "slopenstein") (version "0.1.0") (root "/home/me/games/slopenstein"))
  (mods
    ((id "some-mod") (version "1.0.0") (root "/home/me/games/some-mod"))
  )
)
</code></pre>

Individual save files a script writes are resolved relative to that context root, and the engine rejects anything absolute or containing a `..` segment -- a script can't write (or read) outside its own save tree.
