@page compiling Compiling from source

Even though this engine is being developed as cross-platform and I attempt to test against Linux, Windows, and OSX, I am developing this primarily via a Linux workstation. Since Windows and OSX already have plenty of attention from the commerical market my primary target is Linux users. 

# Requirements

- CMake
- C++20
- git
- Build tools 
    - Windows: Visual Studio
    - Linux & OSX: clang or g++.

At the moment I don't feel like figuring out for each distro which system dependencies you need, so for now I will just print out the `ldd` of the engine binary. 

- linux-vdso.so
- libOpenGL.so
- libGLX.so
- libGLU.so
- libSM.so
- libICE.so
- libX11.so
- libXext.so
- libm.so
- libgcc_s.so
- libc.so
- /lib64/ld-linux-x86-64.so
- libGLdispatch.so
- libstdc++.so
- libuuid.so
- libxcb.so
- libXau.so
- libXdmcp.so

# Getting the source

The main repository is located at https://git.slopnode.net/games/engine. 

```
> git clone https://git.slopnode.net/games/engine.git
Cloning into 'engine'...
remote: Enumerating objects: 4792, done.
remote: Counting objects: 100% (4792/4792), done.
remote: Compressing objects: 100% (2284/2284), done.
remote: Total 4792 (delta 2565), reused 4494 (delta 2385), pack-reused 0 (from 0)
Receiving objects: 100% (4792/4792), 6.74 MiB | 843.00 KiB/s, done.
Resolving deltas: 100% (2565/2565), done.
```

Then fetch the submodule libraries.

```
> git submodule update --init --recursive
Submodule 'lib/JoltPhysics' (https://github.com/jrouwe/JoltPhysics.git) registered for path 'lib/JoltPhysics'
Submodule 'lib/flecs' (https://github.com/SanderMertens/flecs.git) registered for path 'lib/flecs'
Submodule 'lib/imgui' (https://github.com/ocornut/imgui.git) registered for path 'lib/imgui'
Submodule 'lib/raylib' (https://github.com/raysan5/raylib.git) registered for path 'lib/raylib'
Submodule 'lib/rlImGui' (https://github.com/raylib-extras/rlImGui.git) registered for path 'lib/rlImGui'
Submodule 'lib/s7' (https://github.com/abramsba/s7.git) registered for path 'lib/s7'
Submodule 'lib/soloud' (https://github.com/jarikomppa/soloud.git) registered for path 'lib/soloud'
Cloning into '.../engine/lib/JoltPhysics'...
Cloning into '.../engine/lib/flecs'...
Cloning into '.../engine/lib/imgui'...
Cloning into '.../engine/lib/raylib'...
Cloning into '.../engine/lib/rlImGui'...
Cloning into '.../engine/lib/s7'...
Cloning into '.../engine/lib/soloud'...
Submodule path 'lib/JoltPhysics': checked out '765845d66b68a8319d169055e850bd4fb3d902aa'
Submodule path 'lib/flecs': checked out '92685e09deb5885f79a061804d656f67ac388b58'
Submodule path 'lib/imgui': checked out 'b62bfd6b06de958e4630b715225b7e8409bfd0f9'
Submodule path 'lib/raylib': checked out '94897c4eca842673bad16ab03ad776a0a2255b14'
Submodule path 'lib/rlImGui': checked out 'ef129d1858373b6fe332c45f85a1dfc1421bebae'
Submodule path 'lib/s7': checked out '720bb3754539929116cfb7835a0be0e5003c5550'
Submodule path 'lib/soloud': checked out 'e82fd32c1f62183922f08c14c814a02b58db1873'
```

# Building

First the build needs to be configured. By default a debug build is produced. 

```
> cmake -S . -B build
-- The C compiler identification is GNU 16.1.1
-- The CXX compiler identification is GNU 16.1.1
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/bin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Targets: flecs_static
-- Wayland support: enabled
-- Performing Test COMPILER_HAS_THOSE_TOGGLES
-- Performing Test COMPILER_HAS_THOSE_TOGGLES - Success
-- Testing if -Werror=pointer-arith can be used -- compiles
-- Testing if -Werror=implicit-function-declaration can be used -- compiles
-- Testing if -fno-strict-aliasing can be used -- compiles
CMake Warning at lib/raylib/src/CMakeLists.txt:14 (message):
  Default build type is not set (CMAKE_BUILD_TYPE)


-- Setting build type to '' as none was specified.
-- Using raylib's GLFW
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Including Wayland support
-- Including X11 support
-- Looking for memfd_create
-- Looking for memfd_create - found
-- Found PkgConfig: /usr/bin/pkg-config (found version "2.5.1")
-- Checking for modules 'wayland-client>=0.2.7;wayland-cursor>=0.2.7;wayland-egl>=0.2.7;xkbcommon>=0.5.0'
--   Found wayland-client, version 1.25.0
--   Found wayland-cursor, version 1.25.0
--   Found wayland-egl, version 18.1.0
--   Found xkbcommon, version 1.13.2
-- Found X11: /usr/include
-- Looking for XOpenDisplay in /usr/lib/libX11.so;/usr/lib/libXext.so
-- Looking for XOpenDisplay in /usr/lib/libX11.so;/usr/lib/libXext.so - found
-- Looking for gethostbyname
-- Looking for gethostbyname - found
-- Looking for connect
-- Looking for connect - found
-- Looking for remove
-- Looking for remove - found
-- Looking for shmat
-- Looking for shmat - found
-- Looking for IceConnectionNumber in ICE
-- Looking for IceConnectionNumber in ICE - found
-- Performing Test RAYLIB_ATOMICS_WITHOUT_LIBATOMIC
-- Performing Test RAYLIB_ATOMICS_WITHOUT_LIBATOMIC - Success
-- Audio Backend: miniaudio
-- Building raylib static library
-- X11 support enabled for raylib
-- Generated build type: 
-- Compiling with the flags:
--   PLATFORM=PLATFORM_DESKTOP
--   GRAPHICS=GRAPHICS_API_OPENGL_43
-- Configuring done (1.9s)
-- Generating done (0.1s)
-- Build files have been written to: .../engine/build
```

For release builds add the argument `-DCMAKE_BUILD_TYPE=Release`.

From here run the build command and all of the binaries including the tools will be available in the `build/` folder.

```
> cmake --build build -j10
...
[  3%] Built target s7_patched_source
[ 21%] Built target glfw
[ 22%] Built target imgui
[ 38%] Built target soloud
[ 44%] Built target flecs_static
[ 46%] Built target s7
[ 51%] Built target raylib
[ 68%] Built target rlImGui
[ 70%] Built target slopicons
[ 70%] Built target Jolt
[ 76%] Built target sloplib
[ 81%] Built target slopbsp
[ 81%] Built target slopfac
[ 81%] Built target sloprad
[ 81%] Built target slopvis
[ 89%] Built target slopsprite
[ 93%] Built target slopmap
[ 95%] Built target sloptests
[100%] Built target slopengine

> ls build/
CMakeCache.txt
CMakeDoxyfile.in
CMakeDoxygenDefaults.cmake
CMakeFiles
CPackConfig.cmake
CPackSourceConfig.cmake
CTestTestfile.cmake
Doxyfile
Makefile
cmake_install.cmake
compile_commands.json
generated
lib
libsloplib.a
slopbsp
slopengine
slopfac
slopicons
slopmap
sloprad
slopsprite
slopvis
test
```

The `slop*` executables can be launched from either this directory, or they can be installed in your user local environment, or on the system under `/usr/local/bin`.

# Running the demo

A basic demo is included with the engine that can be used to test.

```
> ./build/slopengine --base-game packages/demo
INFO: Initializing raylib 6.1-dev
INFO: Platform backend: DESKTOP (GLFW)
INFO: Supported raylib modules:
INFO:     > rcore:..... loaded (mandatory)
INFO:     > rlgl:...... loaded (mandatory)
INFO:     > rshapes:... loaded (optional)
INFO:     > rtextures:. loaded (optional)
INFO:     > rtext:..... loaded (optional)
INFO:     > rmodels:... loaded (optional)
INFO:     > raudio:.... loaded (optional)
INFO: DISPLAY: Trying to enable VSYNC
INFO: DISPLAY: Device initialized successfully 
INFO:     > Display size: 1920 x 1080
INFO:     > Screen size:  1280 x 720
INFO:     > Render size:  1280 x 720
INFO:     > Viewport offsets: 0, 0
INFO: GLAD: OpenGL extensions loaded successfully
INFO: GL: Supported extensions count: 241
INFO: GL: OpenGL device information:
INFO:     > Vendor:   AMD
INFO:     > Renderer: AMD Radeon 780M Graphics (radeonsi, phoenix, ACO, DRM 3.64, 7.1.3-arch1-2)
INFO:     > Version:  4.6 (Core Profile) Mesa 26.1.4-arch1.1
INFO:     > GLSL:     4.60
INFO: GL: VAO extension detected, VAO functions loaded successfully
INFO: GL: NPOT textures extension detected, full NPOT textures supported
INFO: GL: DXT compressed textures supported
INFO: GL: ETC2/EAC compressed textures supported
INFO: GL: Compute shaders supported
INFO: GL: Shader storage buffer objects supported
INFO: PLATFORM: DESKTOP (GLFW - X11): Initialized successfully
INFO: TEXTURE: [ID 1] Texture loaded successfully (1x1 | R8G8B8A8 | 1 mipmaps)
INFO: TEXTURE: [ID 1] Default texture loaded successfully
INFO: SHADER: [ID 1] Vertex shader compiled successfully
INFO: SHADER: [ID 2] Fragment shader compiled successfully
INFO: SHADER: [ID 3] Program shader loaded successfully
INFO: SHADER: [ID 3] Default shader loaded successfully
INFO: RLGL: Render batch vertex buffers loaded successfully in RAM (CPU)
INFO: RLGL: Render batch vertex buffers loaded successfully in VRAM (GPU)
INFO: RLGL: Default OpenGL state initialized successfully
INFO: TEXTURE: [ID 2] Texture loaded successfully (128x128 | GRAY_ALPHA | 1 mipmaps)
INFO: FONT: Default font loaded successfully (224 glyphs)
INFO: SYSTEM: Working Directory: .../engine
INFO: SHADER: [ID 4] Vertex shader compiled successfully
INFO: SHADER: [ID 5] Fragment shader compiled successfully
INFO: SHADER: [ID 6] Program shader loaded successfully
INFO: RENDER: entering menu (package on-startup / Debug → Map)
INFO: FILEIO: [.../engine/packages/engine/icons/silk.png] File loaded successfully
INFO: IMAGE: Data loaded successfully (512x512 | R8G8B8A8 | 1 mipmaps)
INFO: TEXTURE: [ID 3] Texture loaded successfully (512x512 | R8G8B8A8 | 1 mipmaps)
INFO: TEXTURE: [ID 4] Texture loaded successfully (512x128 | R8G8B8A8 | 1 mipmaps)
```

This should bring up a black screen with a menu bar on the top with a file menu option to select a map.