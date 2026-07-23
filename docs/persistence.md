# Persistence

Player progress, settings, and screenshots live outside packages. Packages are mounted content; the engine keys writeable user data off the mount stack so a given engine, base game, and mod set keep their own tree. Editor tools that write assets back into a package (slopmap, slopsprite) are not this system.

Related: [Package structure](package-structure.md), [Scripting](scripting.md#package-menus), [Scripting](scripting.md#save-io-and-map-flow).

## User config root

The config directory is platform-specific:

```text
Linux:   $XDG_CONFIG_HOME/slopengine  or  ~/.config/slopengine
Windows: %APPDATA%/slopengine
macOS:   ~/Library/Application Support/slopengine
```

If none of those resolve, the engine falls back to `./slopengine-config` next to the process. Under that root you get `settings.cfg` (graphics and controls), a `screenshots/` folder, and `saves/` for package-owned progress blobs.

## Mount stack and save context

At runtime the VFS mounts packages in a fixed order: the engine package first, then the base game from `--base-game`, then each `--mod` in the order given on the command line. Later packages override earlier ones at the same virtual asset path; ids must be unique and every `(depends ...)` entry in `package.meta` must resolve to something mounted. See [Package structure](package-structure.md#mounting).

The same stack builds the save context directory. Paths look like:

```text
<userConfig>/saves/
  <engineId>_<engineVer>/
    <baseId>_<baseVer>/
      <modsSegment>/
```

Each engine or base segment is the package `id` and `version` from `package.meta`, joined with `_`. Characters that are unsafe in paths (`/ \ : * ? " < > |`) become `_`. When no mods are mounted, the final segment is `vanilla`. With mods, it is `id_ver+id_ver+…` in CLI mount order, so changing which mods you load or the order you pass them selects a different tree.

Example with no mods on Linux:

```text
~/.config/slopengine/saves/slopengine.engine_0.1.0/slopdoom_0.1.0/vanilla/
```

Relative paths passed to `(save-write)` and `(save-read)` are resolved under that context root only. Empty paths, absolute paths, and `..` segments are rejected.

## mount.s7

The first successful `(save-write)` into a context also creates `mount.s7` beside the package-chosen files if it is not already there. The sidecar records the exact engine, base, and mod ids, versions, and filesystem roots for that run:

```text
(mount
  (engine (id "…") (version "…") (root "…"))
  (base (id "…") (version "…") (root "…"))
  (mods
    ((id "…") (version "…") (root "…"))))
```

With no mods, `(mods)` is empty. The file is not rewritten on later saves in the same directory.

## What the engine owns vs the package

The engine owns the config root, constructing the mount-scoped save context, jailing relative paths, writing `mount.s7`, and Scheme primitives for save I/O, directory listing under the context, map changes, and player pose. The F1 main menu shell (File / Config / Debug) and Pause window stay engine chrome; packages fill File and Pause slots and draw their own New/Load/Save modals through Scheme ImGui hooks. The engine does not invent campaigns, episodes, checkpoints, or fixed save slots.

Packages choose the relative layout under the context root, the S-expression body schema, when to write, how to restore, and how New/Load/Save look and behave. A package table such as `data/campaign.s7` is only data the package reads; the engine does not register it. Menu and CLI detail is in [Scripting](scripting.md).

## Typical write and restore flow

Package UI or gameplay calls save helpers, then `(save-write rel form)`. Loading typically `(save-read rel)`, stashes the form, and `(request-map-load map "load")`. After the map spawns, `(on-map-ready map-id reason)` runs; packages treat `"load"` as restore and `"fresh"` (the default) as a new run. A common envelope is `(save (version N) (package "id") …)` with package-defined body fields.
