Mime runs Windows programs on macOS by reimplementing Windows itself, from the kernel up.

Unlike Wine, which translates the Windows API into host calls, Mime boots its own NT kernel inside a lightweight virtual machine. Its `ntdll.dll`, `kernel32.dll`, `user32.dll` and the rest are written from scratch, and they make system calls into that kernel with the same numbers as Windows 11. No Microsoft code runs: only data is imported from your ISO, such as fonts, themes, icons and the registry of a fresh install. The long term goal is deep compatibility, up to kernel drivers.

> [!NOTE]
> Mime is at an early stage.

Mime runs ARM64, x64, and experimental [32-bit x86 programs](docs/x86.md). API coverage differs between architectures.

## Getting started

You need macOS 15 or later on Apple silicon, the Homebrew LLVM toolchain and a Windows 11 `arm64` ISO.

```sh
brew install llvm lld
export PATH="$(brew --prefix llvm)/bin:$PATH"

make
build/mime setup ~/Downloads/Windows11_arm64.iso
build/mime run build/examples/messagebox.exe
build/mime down
```

| Command                  | What it does                                                                                                |
|--------------------------|-------------------------------------------------------------------------------------------------------------|
| `mime setup <iso>`       | Import fonts, themes and icons from an ISO.                                                                 |
| `mime up`                | Start the machine in the background.                                                                        |
| `mime run <program.exe>` | Run a program, from a host path or a `C:\` path, the terminal acting as its console like a Windows shell's. |
| `mime status`            | Show whether the machine is running.                                                                        |
| `mime down`              | Stop the machine.                                                                                           |

## Progress

How much of Windows 11 build 26300 Mime implements, counted in exported functions and system services. A function counts once it exists, even if partial: the [documentation](docs/README.md) tells which ones are complete.

| Unit                                | Progress                   | Implemented |
|-------------------------------------|----------------------------|-------------|
| `ntoskrnl.exe` system services      | `████░░░░░░░░░░░░░░░░` 22% | 108 / 489   |
| `ntoskrnl.exe` kernel exports       | `██░░░░░░░░░░░░░░░░░░` 8%  | 270 / 3249  |
| `win32k.sys` system services        | `██░░░░░░░░░░░░░░░░░░` 8%  | 123 / 1499  |
| `ntdll.dll` (without syscall stubs) | `███░░░░░░░░░░░░░░░░░` 14% | 220 / 1542  |
| `kernelbase.dll`                    | `██████░░░░░░░░░░░░░░` 31% | 635 / 2036  |
| `kernel32.dll`                      | `█████████░░░░░░░░░░░` 43% | 735 / 1698  |
| `user32.dll`                        | `██████░░░░░░░░░░░░░░` 28% | 298 / 1050  |
| `gdi32.dll`                         | `███░░░░░░░░░░░░░░░░░` 14% | 138 / 995   |

## Roadmap

- [x] Boot an NT kernel in a Hypervisor.framework virtual machine.
- [x] Load and run Windows programs with the system DLLs.
- [x] Windows, messages, painting and the Windows 11 visual style.
- [x] `MessageBox` with the native Windows 11 look.
- [x] The real registry of the ISO, specialized into a genuine looking installation.
- [ ] Common controls, menus, edit boxes and the clipboard.
- [ ] Running real applications from the ISO.
- [ ] Kernel drivers.
- [ ] Linux on arm64 and x86_64.

## Learn more

- [Contributing](CONTRIBUTING.md): build, run, debug and how Mime works.
- [Documentation](docs/README.md): every reimplemented Windows component, function by function.
