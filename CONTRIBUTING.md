# Contributing

Thanks for your interest. This guide explains how to build and run Mime and the rules the code follows.
For an overview of [how Mime works](README.md#how-mime-works), see the README.

## Requirements

### macOS

```sh
xcode-select --install
```

With [Homebrew](https://brew.sh/) installed, install the build tools and put its LLVM first in `PATH`, since Apple's `clang` cannot produce Windows binaries.

```sh
brew install llvm lld cmake ninja
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

Direct3D and Vulkan programs draw with the Mac's GPU through Mime's own Vulkan driver on Metal 4, built into the `mime` binary from [host/vkmetal](host/vkmetal/). It needs macOS 26 or later; on older systems, Direct3D only has its software device.

### Linux

Running Mime requires x86_64 Linux with KVM enabled and read/write access to `/dev/kvm`.

Building also needs a C++ standard library and its development files, pkg-config, and the SDL3 and Wayland client development libraries.

```sh
sudo pacman -Syu --needed base-devel clang llvm lld cmake ninja sdl3 wayland
```

## Build

From the repository root, configure once with Ninja and build.

```sh
cmake -B build -G Ninja
ninja -C build
```

Build files go in `build/`. After editing code, run `ninja -C build` again; it rebuilds what changed and runs jobs in parallel automatically. To remove build outputs, use `ninja -C build clean`.

The guest always runs the build machine's processor, there is no cross build: ARM64 on ARM64, x64 on x86_64. Linux ARM64 has no virtual machine backend yet, so it builds only the guest.

### Icons and sounds

The repository has no binary assets. The icons, cursors and bitmaps are SVG drawings in [mime-icons](https://code.vexcited.com/mime/icons), and the system sounds are text recipes in [mime-audio](https://code.vexcited.com/mime/audio). CMake renders and synthesizes them into the files Windows reads, with scripts in [scripts/asset](scripts/asset/) that need nothing but CMake. The message tables of `FormatMessage` are built the same way from [data/messages](data/messages/).

At configure time, CMake fetches the commits that [CMakeLists.txt](CMakeLists.txt) pins. A checkout next to Mime's, `../mime-icons` or `../mime-audio`, is used as it is instead. That includes uncommitted changes, so a drawing can be edited and rebuilt without a commit. Worktrees look next to their main checkout. To build from a checkout somewhere else, set `-DMIME_ICONS_DIR=<path>` or `-DMIME_AUDIO_DIR=<path>`. After pushing a change to either repository, update its pinned commit in `CMakeLists.txt`.

The first build renders every icon and takes a minute or two of CPU across all cores. After that, only what changed is rebuilt.

## Running

```sh
build/mime run /path/to/program.exe
build/mime down
```

| Path                              | Content                                |
|-----------------------------------|----------------------------------------|
| `~/.local/share/mime/drive_c`     | The C: drive seen by Windows programs. |
| `~/.local/state/mime/vmm.log`     | Kernel and machine log.                |

Only one machine runs per user, it shows as `mime-vmm` in any process manager.
