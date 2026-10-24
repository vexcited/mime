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

## Running

```sh
build/mime setup /path/to/windows.iso
build/mime run /path/to/program.exe
build/mime down
```

| Path                              | Content                                |
|-----------------------------------|----------------------------------------|
| `~/.local/share/mime/drive_c`     | The C: drive seen by Windows programs. |
| `~/.local/state/mime/vmm.log`     | Kernel and machine log.                |

Only one machine runs per user, it shows as `mime-vmm` in any process manager.
