# Icons

Icons are packed UI atlases: a PNG sheet plus an `.iconmap` that names rectangles inside it. They are for tools and interface chrome (menus, buttons, tree rows), not world sprites. World billboards use [Sprites](sprites.md).

Package layout is summarized in [Package structure](package-structure.md).

## Assets

| Kind | Extension | Directory | Virtual path example |
|------|-----------|-----------|----------------------|
| Icon atlas texture | `.png` | `icons/` | `silk` → `icons/silk.png` |
| Icon map | `.iconmap` | `icons/` | `silk` → `icons/silk.iconmap` |
| Source icons (authoring) | `.png` | `icons/<set>/` | `icons/silk/accept.png` (not loaded at runtime) |

A set is identified by a virtual path without extension (for example `silk`). The map and atlas texture share that path. Nested folders under a source tree become `/`-separated icon ids when packed.

## Icon map (`.iconmap`)

```text
(iconmap
  (atlas "silk")
  (size 512 512)
  (icon "accept" 0 0 16 16)
  (icon "add" 16 0 16 16)
  (icon "folder" 32 0 16 16))
```

| Field | Meaning |
|-------|---------|
| `(atlas "path")` | Atlas texture virtual path under `icons/` (no extension). Usually the same as the set name. |
| `(size W H)` | Atlas pixel size. |
| `(icon "id" X Y W H)` | Named rectangle in atlas pixels. |

The parser is line-oriented: known field lines matter; the wrapping `(iconmap` / `)` are ignored.

## Packing with `slopicons`

Source PNGs live under `icons/<set-name>/`. The `slopicons` tool packs them into `<set-name>.png` and `<set-name>.iconmap` next to that folder:

```bash
./build/slopicons pack silk packages/engine/icons
```

That reads `packages/engine/icons/silk/**/*.png` and writes `packages/engine/icons/silk.png` plus `packages/engine/icons/silk.iconmap`. Icon ids are relative paths under the source folder without `.png` (for example `accept`, or `toolbar/save` if nested).

CMake also exposes a convenience target for the base Silk set:

```bash
cmake --build build --target pack-silk-icons
```

Commit the packed atlas and `.iconmap` with the package. Runtime loading only needs those two files; the source folder is for regenerating the pack.

## Runtime

`AssetStore` loads a set by virtual path:

- `hasIconAtlas(set)`: true when `icons/<set>.iconmap` exists
- `getIconAtlas(set)`: parses the map, loads `icons/<atlas>.png`, caches the GPU texture and rect table
- `getIconRect(set, id)`: source rectangle for one icon id
- `drawIcon(set, id, position, size)`: draws with Raylib (`DrawTexturePro`)

Tools such as `slopmap` use the same atlas through ImGui helpers (`drawIconImGui`, menu/button wrappers) with default set `silk`.

## Engine package

`packages/engine/icons/` ships the FamFamFam Silk set as `silk`: source PNGs under `icons/silk/`, plus packed `silk.png` and `silk.iconmap`. Other packages can add more sets the same way, or override `silk` by mounting a later package with the same virtual path.
