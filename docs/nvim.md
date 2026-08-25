@page nvim Neovim

Syntax highlighting for package assets (`.csg`, `.meta`, `.mat`, `.s7`, `.geo`, `.anim`, `.skel`, `.spr`, `.spanim`, `.prt`, `.iconmap`) and the engine's `.glsl` shaders, mirroring the VSCode extension (see @ref vscode). It's a plain Neovim runtime plugin at `tools/nvim` in the repository, with no external dependencies, so it's pointed at directly rather than pulled from a plugin registry.

# Install {#install}

lazy.nvim:

```lua
{ dir = "~/repos/engine/tools/nvim", name = "slopengine.nvim", lazy = false }
```

packer.nvim:

```lua
use { "~/repos/engine/tools/nvim" }
```

Vanilla, without a plugin manager:

```vim
set runtimepath+=~/repos/engine/tools/nvim
filetype plugin on
syntax on
```

Adjust the path if your checkout of the repository lives somewhere other than `~/repos/engine`.

# Verifying {#verifying-nvim}

Open any `.s7` or `.csg` file from a package. Comments, strings, numbers, booleans, quote forms, and keyword or builtin calls in head position should be highlighted. Opening a `.glsl` shader should highlight comments, preprocessor directives, storage qualifiers, types, and builtin variables and functions.
