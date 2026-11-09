## Installation

Mime is at an early stage, you should build from source for now.

## Usage

```sh
mime run ~/program.exe
mime run notepad          # anything the windows run box opens
mime apps                 # the start menu's apps, `mime run <id>` opens one
mime uninstall <id>       # an app's id, or a program's from `mime apps --installed`
mime features             # the .NET Framework, the WebView2 runtime and the Microsoft fonts, fetched from Microsoft when needed
mime features enable <name>
mime down
```

## How ?

Mime runs its own Windows-compatible kernel and DLLs inside a virtual machine.
Windows programs use those DLLs and system calls to access files, create windows and do their work.

- The [host](host/), connects the virtual machine to macOS or Linux. The `mime` command talks over a Unix socket to `mime-vmm`, a background process running the same binary. It uses [Hypervisor.framework](https://developer.apple.com/documentation/hypervisor) on macOS and KVM on x86_64 Linux to run the guest kernel. Graphics run on the host's GPU: the guest's Direct3D is translated to Vulkan, whose calls the host runs on its Vulkan driver, Mime's own on Metal 4 on macOS.
- The [guest kernel](guest/ntos/), manages processes, threads, memory, files and the registry. It loads `win32k.sys` to handle window management, input and GDI drawing. When it needs host services, such as file access, window display, networking, audio or GPU work, it makes hypercalls through Mime's [host interface](include/mime/abi.h).
- The [Windows DLLs](guest/), implement the APIs programs expect. They use `ntdll.dll` and `win32u.dll` to make system calls into the guest kernel when needed.

The guest kernel runs on the host's processor architecture. On arm64, Mime's `xta64.dll` translates x64 and x86 program code to run on arm64. On x86_64 Linux, x64 code runs directly and 32-bit x86 code runs directly through Mime's WOW64 layer, which adapts its system calls to the 64-bit kernel.

Mime needs no copy of Windows. The first `mime` command creates the `C:` drive. The registry, theme, icons, cursors, sounds and system strings are Mime's own and live in the ROM: `regc` builds the hives from `data/registry`. What Windows ships but Mime cannot, the .NET Framework, the Edge WebView2 runtime and the Microsoft fonts, is fetched from Microsoft when a program first needs it, after asking; `mime features` lists them. A program that needs a missing font is ended if the font is not fetched.

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions and contribution guidelines.
