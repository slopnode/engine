@page cli Command line arguments

`slopengine` and the `slop*` tools share one small argument parser. It understands a fixed set of "mount" flags for locating packages, and hands anything it doesn't recognize to the base package, which can declare its own flags in `data/cli.s7`.

# Mount flags {#mount-flags}

```
slopengine --base-game <path|name> [--mod <path|name>]... [--profile <name>] [--debug] [package-flags...]
```

- `--base-game` -- the game package to run. Either a directory containing a `package.meta`, or a name looked up in the configured search paths. Required.
- `--mod` -- an additional package mounted on top of the base game, same lookup rules as `--base-game`. Repeatable; mods apply in the order given, later mods overriding earlier ones for the same asset path.
- `--profile` -- selects which settings/saves/screenshots directory tree to use. Defaults to `"default"`. See @ref profiles for what this scopes.
- `--debug` -- enables the developer-only UI (the Debug menu, entity inspector, and related overlays). Off by default so a normal player never sees it.

Where a `--base-game`/`--mod` value resolves to on disk is covered in @ref packages, since it's the same search path logic package dependency resolution uses.

Running with no arguments, or an unrecognized flag before `--base-game` is satisfied, prints usage and exits:

```
Usage: slopengine --base-game <path|name> [--mod <path|name>]... [--profile <name>] [package-flags...]

  --base-game   Base game package: a directory path, or a name looked up in
                the configured search paths (required)
  --mod         Additional mod package: path or name, same lookup as
                --base-game (repeatable)
  --profile     Settings/saves/screenshots profile to use (default: "default")
  --debug       Enable developer-only UI (e.g. the Debug menu)
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

Note that this schema is only known once the base package's own scripts have loaded, so package flags are parsed after `--base-game` is mounted -- a package flag typo doesn't prevent the mount flags themselves from being validated, but it does stop the game from starting.

# Tool binaries {#tool-binaries}

`slopbsp`, `slopfac`, and `slopvis` accept the same `--base-game [--mod]...` mount flags plus a required `--map <name>` identifying which map to process:

```
slopbsp --base-game path/to/game --map my-room
```

`slopmap` and `slopsprite` parse their own arguments (`--target`, an optional `--map`, and so on) rather than going through the package-flag schema, since they're editors rather than something a base package configures. See their individual pages under @ref tools for the flags each one takes.
