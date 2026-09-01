@page vscode VSCode

Syntax highlighting for package assets (`.map`, `.meta`, `.mat`, `.s7`, `.geo`, `.anim`, `.skel`, `.spr`, `.spanim`, `.prt`, `.iconmap`) and the engine's `.glsl` shaders. The extension lives at `tools/vscode` in the repository and isn't published to the marketplace, so it's installed straight from a checkout.

# Install by symlink {#install-symlink}

This is the easiest way to keep the extension in sync with the repository — VSCode loads unpacked extensions directly out of its extensions folder, so pointing a symlink there picks up any future changes without reinstalling.

Linux and macOS:

```
> ln -s ~/repos/engine/tools/vscode ~/.vscode/extensions/slopengine.slopengine-0.2.0
```

Windows (PowerShell):

```
> New-Item -ItemType SymbolicLink -Path "$env:USERPROFILE\.vscode\extensions\slopengine.slopengine-0.2.0" -Target "C:\repos\engine\tools\vscode"
```

Adjust the source path if your checkout lives somewhere other than `~/repos/engine`. Restart VSCode, or run Developer: Reload Window from the command palette, and the extension will be active.

# Install as a packaged extension {#install-packaged}

If a symlink isn't an option, the extension can be packaged into a `.vsix` and installed normally instead. This copies the extension in rather than linking it, so it won't pick up later changes until it's repackaged and reinstalled.

```
> cd tools/vscode
> npx @vscode/vsce package
> code --install-extension slopengine-0.2.0.vsix
```

# Verifying {#verifying}

Open any `.s7` or `.map` file from a package. The language mode in the bottom-right status bar should read Slopengine, and keywords, strings, comments, and numbers should be highlighted. Opening a `.glsl` shader should highlight it as Slopengine GLSL.
