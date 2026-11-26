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
sudo pacman -Syu --needed base-devel clang llvm lld cmake ninja wayland libxkbcommon libpipewire
```

#### NixOS

```sh
nix develop
```

The shell has the toolchain and libraries, the build below works as is.

## Build

```sh
cmake -B build -G Ninja
cmake --build build
```

## Run

```sh
sudo build/mime up
build/mime run /path/to/program.exe
build/mime down
```

`up` runs as root because it reads this machine's firmware tables from `/sys/firmware` for the guest, then drops back to you.

| Path                              | Content                                |
|-----------------------------------|----------------------------------------|
| `~/.local/share/mime/drive_c`     | The C: drive seen by Windows programs. |
| `~/.local/state/mime/vmm.log`     | Kernel and machine log.                |

Each set of XDG directories gives a machine of its own, so machines started with another `XDG_*_HOME`/`XDG_RUNTIME_DIR` run side by side. Two starts on the same directories meet on the same lock, and one machine per set runs.
