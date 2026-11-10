## Requirements

### macOS >=26

```sh
xcode-select --install
brew install llvm lld cmake ninja

# apple's clang can't produce windows binaries so force to use the one from llvm.
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

### Linux

Running Mime requires x86_64 Linux with KVM enabled and RW access to `/dev/kvm`.

#### Arch Linux

```sh
sudo pacman -Syu --needed base-devel clang llvm lld cmake ninja sdl3 wayland
```

#### NixOS

```sh
nix develop
```

The shell has the toolchain and libraries, the build below works as is.
To build the package instead, run `nix build`, the binary lands in `result/bin/mime`.

## Build

```sh
cmake -B build -G Ninja
cmake --build build
```

## Run

```sh
build/mime run /path/to/program.exe
build/mime down
```

| Path                              | Content                                |
|-----------------------------------|----------------------------------------|
| `~/.local/share/mime/drive_c`     | The C: drive seen by Windows programs. |
| `~/.local/state/mime/vmm.log`     | Kernel and machine log.                |

Only one machine runs per user, it shows as `mime-vmm`.
