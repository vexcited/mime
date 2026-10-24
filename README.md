## Installation

Mime is at an early stage, you should build from source for now.

## Usage

```sh
mime setup ~/windows[_arm64].iso
mime run ~/program.exe
mime down
```

## How ?

Mime runs its own Windows-compatible kernel and DLLs inside a virtual machine.
Windows programs use those DLLs and system calls to access files, create windows and do their work.

- The [host](host/), connects the virtual machine to macOS or Linux. The `mime` command talks over a Unix socket to `mime-vmm`, a background process running the same binary. It uses [Hypervisor.framework](https://developer.apple.com/documentation/hypervisor) on macOS and KVM on x86_64 Linux to run the guest kernel.
- The [guest kernel](guest/ntos/), manages processes, threads, memory, files and the registry. It loads `win32k.sys` to handle window management, input and GDI drawing. When it needs host services, such as file access, window display, networking, audio or GPU work, it makes hypercalls through Mime's [host interface](include/mime/abi.h).
- The [Windows DLLs](guest/), implement the APIs programs expect. They use `ntdll.dll` and `win32u.dll` to make system calls into the guest kernel when needed.

The guest kernel runs on the host's processor architecture. On arm64, Mime's `xta64.dll` translates x64 and x86 program code to run on arm64. On x86_64 Linux, x64 code runs directly and 32-bit x86 code runs directly through Mime's WOW64 layer, which adapts its system calls to the 64-bit kernel.

`mime setup` reads your Windows ISO to create the `C:` drive and import fonts, themes, icons and registry hives.

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions and contribution guidelines.
