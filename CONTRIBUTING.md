# Contributing

Thanks for your interest. This guide explains how to build and run Mime and the rules the code follows.
For an overview of [how Mime works](README.md#how-mime-works), see the README.

## Building

Mime needs CMake 3.30 or later, Ninja, and LLVM's `clang` 19 or later, `lld`, `llvm-rc` and `llvm-dlltool`.
Clang compiles both the host program and the Windows side.

### macOS

Running Mime requires an Apple silicon Mac. Install Apple's Command Line Tools if they are missing:

```sh
xcode-select --install
```

With [Homebrew](https://brew.sh/) installed, install the build tools and put its LLVM first in `PATH`,
since Apple's `clang` cannot produce Windows binaries:

```sh
brew install llvm lld cmake ninja
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

### Linux

Running Mime requires x86_64 Linux with KVM enabled and read/write access to `/dev/kvm`.
Building also needs a C++ standard library and its development files, pkg-config, and the SDL3 and
Wayland client development libraries.

On Arch Linux, update the system and install the requirements:

```sh
sudo pacman -Syu --needed base-devel clang llvm lld cmake ninja sdl3 wayland
```

`base-devel` includes GCC's C++ development files and `pkgconf`, which provides pkg-config.
The separate `llvm` package supplies `llvm-rc` and `llvm-dlltool`. Arch's library packages include
their development files, so no separate `-dev` packages are needed.

The optional libsystemd development files enable tray icons and notifications. On Arch they come
with `systemd`, normally already installed; if needed, install it with `sudo pacman -S --needed systemd`.
On other distributions, install the equivalent packages, keeping the minimum tool versions above.

### Build

From the repository root, configure once and build:

```sh
cmake --preset default
ninja -C build
```

The preset selects Ninja and keeps build files in `build/`. After editing code, run `ninja -C build`
again; it rebuilds what changed and runs jobs in parallel automatically. To remove build outputs,
use `ninja -C build clean`. Cleaning is not needed for normal rebuilds.

The default guest is ARM64 on macOS and x64 on x86_64 Linux. Linux ARM64 has no virtual machine
backend yet, so it builds only the guest. To build only the ARM64 guest on either platform:

```sh
cmake --preset arm64
ninja -C build-arm64
```

`build/compile_commands.json` gives editors the compiler flags. `gen/` holds committed tables and
export definitions; files generated during the build go in `build/gen/`.

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

## Rules

- Mime is written from scratch. Never copy code from Microsoft, Wine, ReactOS or any other project.
- A number or behavior that is guessed must say so.

## Code style

- C23: `nullptr`, `bool`, `static_assert`, `typeof`, `constexpr`, `[[attributes]]`, `= {}`, `#embed`,
  `<stdckdint.h>`. Four spaces, braces on the same line except for functions.
- Mime's own names use the `Mime`/`mime_` prefixes; Windows functions keep
  their Windows names and signatures.
- The build must stay free of warnings.

## Commits

One line in English using conventional commits, for example
`feat: add NtUserGetClipboardData`. Keep each commit focused on one change.
