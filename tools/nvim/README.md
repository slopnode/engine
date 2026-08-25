# slopengine.nvim

Neovim syntax highlighting for Slopengine package assets (`.csg`, `.meta`,
`.mat`, `.s7`, `.geo`, `.anim`, `.skel`, `.spr`, `.spanim`, `.prt`,
`.iconmap`, `.texanim`, `.saudio`) and the engine's `.glsl` shaders. Mirrors
the VSCode extension at [`tools/vscode`](../vscode).

- `.csg`, `.meta`, `.mat`, `.s7`, `.geo`, `.anim`, `.skel`, `.spr`,
  `.spanim`, `.prt`, `.iconmap`, `.texanim`, `.saudio` are highlighted as
  the `slopengine` filetype (s7/Scheme-flavored: comments, strings,
  numbers, booleans, quote forms, and keyword/builtin/function calls in
  head position).
- `.glsl` is highlighted as the `slopengine_glsl` filetype (comments,
  preprocessor directives, storage qualifiers, types, builtin
  variables/functions, and function calls).

## Install

This is a plain Neovim runtime plugin (no external dependencies) — point
your plugin manager at this directory.

**lazy.nvim**

```lua
{ dir = "~/repos/engine/tools/nvim", name = "slopengine.nvim", lazy = false }
```

**packer.nvim**

```lua
use { "~/repos/engine/tools/nvim" }
```

**vanilla (no plugin manager)**

```vim
set runtimepath+=~/repos/engine/tools/nvim
filetype plugin on
syntax on
```

Adjust the path if your checkout of this repo lives somewhere other than
`~/repos/engine`.
