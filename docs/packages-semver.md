@page packages Packages

# Meta file

A package is a folder with a `package.meta` at its root:

<pre><code class="language-scheme">(package
  (id "slopenstein")
  (name "Slopenstein")
  (version "0.1.0")
  (depends "slopengine.engine@>=0.4.0"))
</code></pre>

`id` is required and must be unique across everything mounted in one run -- the engine package, the base game, and every mod. Two mounted packages sharing an id is a startup error naming both roots involved.

# Resolving packages

The engine package itself is resolved first and separately from everything else, since it's always mounted before the base game is even looked up. The engine tries, in order: `./packages/engine` relative to the current working directory, then the `SLOPENGINE_ENGINE` environment variable, then a path baked into the binary at build time (`SLOPENGINE_ENGINE_PACKAGE_DIR`, a CMake cache variable defaulting to `<source>/packages/engine`). The first candidate that actually contains a `package.meta` wins.

A value is used as-is if it's a path (relative to the current directory, or absolute) that directly contains a `package.meta`. Otherwise it's treated as a name and looked up as `<search-root>/<name>` across the configured search paths, in priority order:

1. `search_path=` entries from the global `settings.cfg` (see @ref profiles), highest priority, checked in the order they're listed.
2. Directories baked into the binary at build time via the CMake cache variable `SLOPENGINE_APP_SEARCH_PATHS` (semicolon-separated in the CMake invocation) -- how a downstream build points a packaged game at wherever it lays out its own game/mod directories.

The first search root that has `<root>/<name>/package.meta` wins. If nothing matches, the name is returned unchanged, so the "package not found" error the caller raises still shows exactly what was typed rather than some intermediate resolved path.

# Dependencies

`depends` lists package ids that must also be mounted. Each entry is either a bare id, or `id@constraint`:

<pre><code class="language-scheme">(depends "slopengine.engine@>=0.4.0" "some-shared-content")
</code></pre>

`slopengine.engine@>=0.4.0` requires the engine package's mounted version to satisfy `>=0.4.0`. `some-shared-content` with no `@constraint` accepts whatever version of that package is mounted, exactly as if no version tracking existed at all.

Dependency checking happens once, after every package (engine, base, mods) is mounted and duplicate ids are ruled out. A missing dependency or a version outside its constraint is a hard startup error:

```
package 'slopenstein' depends on missing package 'some-shared-content'
package 'slopenstein' requires 'slopengine.engine@>=0.4.0' but found version '0.3.9'
```

## Constraint syntax

A constraint is an optional comparison operator followed by a version: `=`, `>=`, `>`, `<=`, `<`, or no operator at all (which means `=`). An empty constraint always matches.

A version is `MAJOR[.MINOR[.PATCH]]` -- missing components default to `0`, so `"1.2"` parses the same as `"1.2.0"`. Anything with a non-numeric component (`"1.x.0"`) fails to parse, and a constraint that fails to parse -- on either side -- never matches rather than matching by accident:

```
"0.4.3", ""                -> true   (no constraint)
"0.4.3", ">=0.4.0"         -> true
"0.4.0", ">=0.4.0"         -> true  (inclusive)
"0.3.9", ">=0.4.0"         -> false
"0.4.0", "0.4.0"           -> true  (no operator means =)
"0.4.1", "0.4.0"           -> false
"not-a-version", ">=0.4.0" -> false
```

Comparison is purely numeric major/minor/patch -- there's no pre-release or build-metadata concept (`1.0.0-rc1` isn't a version this parser understands at all, it just fails to parse).

# Assets

Once mounted, a package's content is looked up by asset kind (`sprites/`, `textures/`, `data/`, `scripts/`, and so on), with later mods overriding earlier packages for the same path. See @ref assets for the full kind/directory/extension reference and the override rules.