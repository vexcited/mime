## Installation

Mime is at an early stage. Releases ship a binary for x86_64 Linux, on any other platform build from source, see [CONTRIBUTING.md](CONTRIBUTING.md).

Mime needs RW access to `/dev/kvm`, add your user to the `kvm` group if it is not world writable.

### NixOS

The flake installs the binary of the last release, nothing is compiled. Add it to your system flake:

```nix
{
  inputs.mime.url = "git+https://code.vexcited.com/mime/mime.git";

  outputs = { nixpkgs, mime, ... }: {
    nixosConfigurations.<hostname> = nixpkgs.lib.nixosSystem {
      modules = [
        mime.nixosModules.default
        { programs.mime.enable = true; }
      ];
    };
  };
}
```

Then rebuild with `sudo nixos-rebuild switch`, the module enables `hardware.graphics` for the Vulkan drivers.

Move to a new release with `nix flake update mime`.

### Other distributions

```sh
# on arch linux, for example
sudo pacman -S --needed sdl3 wayland curl vulkan-icd-loader

# download the x86_64 binary
curl -L https://github.com/vexcited/mime/releases/latest/download/mime-x86_64-linux.tar.gz | tar -xz
./mime run notepad
```

Move `mime` anywhere in your `PATH` to keep it, and run the same command again to update it.

## Usage

```sh
mime setup

mime run ~/program.exe
mime run notepad

mime apps
mime apps --installed

mime uninstall <id>

mime features
mime features enable <name>

mime down
```

## Configuration

Mime reads `$XDG_CONFIG_HOME/mime/config.ini`, which defaults to `~/.config/mime/config.ini`.
It is read when the machine starts, so run `mime down` after a change.

### Default Values

```ini
[desktop]
shortcuts = false
protocols = true
```

### Keys

| Section     | Key         | Type      | Default | Plaform | Description |
| ----------- | ----------- | --------- | ------- | ------- | ----------- |
| `[desktop]` | `shortcuts` | `boolean` | `false` | Linux   | Exports the Start Menu and desktop shortcuts of Windows programs as `.desktop` entries in `$XDG_DATA_HOME/applications`, so launchers list them. Entries follow the shortcuts as programs are installed or removed. |
| `[desktop]` | `protocols` | `boolean` | `true` | Linux   | Makes Mime the handler of the URL protocols that Windows programs register and that no program of the host answers, so a page of the host's browser can open them. A sign in that ends in the browser comes back to the program this way. |

### Values

- `boolean` accepts `true`, `yes`, `on` and `1`

## How ?

Mime runs its own Windows-compatible kernel and DLLs inside a virtual machine.
Windows programs use those DLLs and system calls to access files, create windows and do their work.

- The [host](host/), connects the virtual machine to macOS or Linux. The `mime` command talks over a Unix socket to `mime-vmm`, a background process running the same binary. It uses [Hypervisor.framework](https://developer.apple.com/documentation/hypervisor) on macOS and KVM on x86_64 Linux to run the guest kernel. Graphics run on the host's GPU: the guest's Direct3D is translated to Vulkan, whose calls the host runs on its Vulkan driver, Mime's own on Metal 4 on macOS.
- The [guest kernel](guest/ntos/), manages processes, threads, memory, files and the registry. It loads `win32k.sys` to handle window management, input and GDI drawing. When it needs host services, such as file access, window display, networking, audio or GPU work, it makes hypercalls through Mime's [host interface](include/mime/abi.h).
- The [Windows DLLs](guest/), implement the APIs programs expect. They use `ntdll.dll` and `win32u.dll` to make system calls into the guest kernel when needed.

The guest kernel runs on the host's processor architecture. On arm64, Mime's `xta64.dll` translates x64 and x86 program code to run on arm64. On x86_64 Linux, x64 code runs directly and 32-bit x86 code runs directly through Mime's WOW64 layer, which adapts its system calls to the 64-bit kernel.

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions and contribution guidelines.

## Community

If you're running into any issue or asking for any feature, please feel free to [open an issue](https://code.vexcited.com/mime/mime/issues) or [join Mime's Discord server](https://discord.gg/SfH2VgX2aq) to come talk with us!
