# Contributing to Mime

Thanks for your interest. This guide explains how to build and run Mime, how it works in the big lines, and the few rules the code follows.

## Building

Mime builds with CMake and Ninja, and LLVM's clang 19 or later, which compiles both the host program and
the Windows side. Apple's own `clang` cannot produce Windows binaries, so on macOS put Homebrew's first in
`PATH`.

```sh
brew install llvm lld cmake ninja
export PATH="$(brew --prefix llvm)/bin:$PATH"
cmake --preset default
cmake --build --preset default
```

On Linux the build needs LLVM itself besides clang and lld: `llvm-nm`, `llvm-rc`, `llvm-strip`,
`llvm-readobj` and `llvm-dlltool` come from it, `llvm` on Arch Linux. The guest runs the host's processor:
arm64 on macOS, x64 on Linux x86_64. Linux arm64 has no virtual machine backend yet, so there the build
makes the arm64 guest only. `cmake --preset arm64` builds the arm64 guest on any machine, into `build-arm64`.

| Target                                  | Output                                                                                    |
|-----------------------------------------|-------------------------------------------------------------------------------------------|
| `cmake --build build`                   | Everything below but the tests.                                                           |
| `cmake --build build --target mime`     | `build/mime`, the command line program with the system files embedded, ad hoc signed.     |
| `cmake --build build --target guest`    | `build/guest`: `ntoskrnl.exe`, `win32k.sys` and the system DLLs of each processor.        |
| `cmake --build build --target examples` | `build/examples/x64/*.exe`, one program per file in `examples/`, and arm64 ones on macOS. |
| `cmake --build build --target tests`    | `build/tests/<processor>/*.exe`, the test programs.                                       |
| `cmake --build build --target test-x86` | Builds and runs the 32-bit suite, `tools/runtime32/run.py`.                               |
| `cmake --build build --target test-console` | Runs the x64 tests that need standard streams (`stdio`, on a pipe) and a terminal (`coninput`, on a pseudo terminal), `tools/runtime32/run_console.py`. |
| `cmake --build build --target clean`    | Removes what the build made.                                                              |

`build/compile_commands.json` gives editors the flags of every file.

## Running

```sh
build/mime setup /path/to/Windows11_arm64.iso   # once
build/mime run build/examples/messagebox.exe    # starts the machine if needed
build/mime down
```

`build/mime` is self contained: the kernel, drivers and DLLs are embedded in
it and mapped into the virtual machine, never written to disk, so it is the
only file to distribute. The machine uses the files of the binary that
started it, so to test a change: `cmake --build build`, `mime down`, then `mime run` again.

Mime follows the XDG base directories:

| Path                              | Content                                |
|-----------------------------------|----------------------------------------|
| `~/.local/share/mime/drive_c`     | The C: drive seen by Windows programs. |
| `~/.local/state/mime/vmm.log`     | Kernel and machine log.                |

Only one machine runs per user. It shows as `mime-vmm` in `ps`. Mime needs
macOS 15 or later for the framework's virtual interrupt controller.

### Debugging

- Read `~/.local/state/mime/vmm.log`: the kernel and `DbgPrint` write there.
- `pkill -USR1 mime-vmm` dumps the registers, a backtrace and the count of
  each kind of exit to the host into the log. It locates a hang, and exits
  are what to watch when working on speed: an idle machine makes none. On
  Linux the process is named `mime`: `kill -USR1 $(pgrep -f '^mime-vmm')`.
  The guest then also logs every thread of every process: its state, the
  object it waits for (and in how many milliseconds a timed wait ends), where its user code called the kernel and the return
  address on its stack, which `llvm-symbolizer` turns into the call that
  blocks.
- On Linux, `kill -PROF $(pgrep -f '^mime-vmm')` starts sampling where the
  virtual CPUs run, about 4000 times a second each, and a second one stops
  and logs the 400 hottest addresses with their counts. A user mode address
  shows as the module it falls in and its offset, found from the image
  headers in guest memory; add the module's image base to symbolize it.
  Idle processors show as the kernel's halt.
- Guest binaries carry DWARF debug info and are linked at fixed addresses, so
  a guest address symbolizes directly:
  `llvm-symbolizer --obj=build/guest/system32/ntoskrnl.exe 0xfffff80000212345`.

## How Mime works

```
 macOS                                                         
 ┌──────────────────────────────────────────────────────────┐
 │ mime (command line) ── unix socket ── mime-vmm (machine) │
 │                                         │ Hypervisor.framework
 │                                         │ Cocoa windows, host files
 └─────────────────────────────────────────┼────────────────┘
                                           │ hypercalls (hvc #0)
 virtual machine, arm64                    │
 ┌─────────────────────────────────────────┼────────────────┐
 │ kernel mode   ntoskrnl.exe ── win32k.sys┘                │
 │                    ▲             ▲                       │
 │                    │ syscalls    │ syscalls              │
 │ user mode     ntdll.dll       win32u.dll                 │
 │                    ▲             ▲                       │
 │               kernelbase.dll  user32.dll, gdi32.dll      │
 │                    ▲             ▲                       │
 │               kernel32.dll       │                       │
 │                    ▲             │                       │
 │                   program.exe ───┘                       │
 └──────────────────────────────────────────────────────────┘
```

**The host** (`host/`) is a single `mime` binary. The command line talks to a
background machine process over a unix socket. That process creates a
Hypervisor.framework virtual machine, loads `ntoskrnl.exe` into it and runs
its virtual CPUs. The guest kernel reaches the host only through a small set
of hypercalls defined in [`include/mime/abi.h`](include/mime/abi.h): logging,
file access to the C: drive, input events and window presentation, which the
host shows as native Cocoa windows. Everything else stays inside the VM: the
timer and interrupts go through the framework's virtual GIC, and the system
files are mapped into guest memory, so an idle machine never wakes the host. `mime setup` reads the ISO directly
(UDF, WIM and LZX decoders in `host/setup`) to import fonts, themes and icons.

**The kernel** (`guest/ntos`) is an NT kernel written from scratch: scheduler,
memory manager, object manager, processes and threads, I/O manager with a file
system backed by the host, the registry and access tokens. It loads the
registry hives `mime setup` imported from the ISO and specializes them into
this machine's installation at every boot, and loads `win32k.sys` as a
driver, like Windows.

**System calls use the real Windows numbers.** `tools/syscalls` holds the
service tables of Windows 11 build 26300 arm64. Windows numbers its services
the same on every architecture: the x64 `ntdll.dll` and `win32u.dll` of build
26300 carry exactly these numbers, so the same tables serve the x64 build.
`tools/gensyscalls.c` turns them into the stubs of `ntdll.dll` and
`win32u.dll`, with the bytes of Windows' own stubs on x64, and
`tools/genservices.c` registers every `Nt*` function the kernel defines. A
service Mime does not implement yet returns `STATUS_NOT_IMPLEMENTED`. As on
x64 Windows, the x64 `NtQuerySystemTime` reads the shared page instead of
entering the kernel.

**The user mode DLLs** (`guest/ntdll`, `guest/kernelbase`, `guest/user32`,
`guest/gdi32`) reimplement the Windows API on top of those system calls, with
the same layering as Windows: `kernel32.dll` only forwards to `kernelbase.dll`,
and `user32.dll` calls into `win32k.sys`, which calls back into `user32.dll`
for window procedures.

**Drawing** happens in `win32k.sys`, with its own TrueType rasterizer, PNG
decoder and Windows visual style parser in `guest/lib`, using the fonts and the
`aero.msstyles` theme imported from your ISO.

## Repository layout

| Path                                                                                    | Content                                                                                                                                |
|-----------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------|
| `host/`                                                                                 | The macOS program: command line, setup, VM and UI.                                                                                     |
| `include/mime/`                                                                         | The host and guest ABI.                                                                                                                |
| `guest/ntos/`                                                                           | `ntoskrnl.exe`: `ke` core, `mm` memory, `ob` objects, `ps` processes, `io` files, `cm` registry, `se` security, `ex` executive, `hal`. |
| `guest/win32k/`                                                                         | `win32k.sys`, window manager and GDI.                                                                                                  |
| `guest/ntdll/`, `guest/kernelbase/`, `guest/kernel32/`, `guest/user32/`, `guest/gdi32/` | The system DLLs.                                                                                                                       |
| `guest/rtl/`, `guest/crt/`                                                              | Runtime shared by the kernel and `ntdll.dll`.                                                                                          |
| `guest/lib/`                                                                            | TrueType, PNG and visual style libraries.                                                                                              |
| `guest/include/`                                                                        | Windows and NT headers, written for Mime.                                                                                              |
| `tools/`                                                                                | Build generators (syscalls, embedded files, version resources, checksums), syscall tables and `progress.sh`.                           |
| `CMakeLists.txt`, `cmake/`                                                              | The build.                                                                                                                             |
| `examples/`                                                                             | Small Windows programs built against Mime.                                                                                             |
| `docs/`                                                                                 | One page per Windows component.                                                                                                        |

## Reverse engineering rules

- Behavior is studied on real Windows binaries, with a disassembler or by
  observing them. Never copy code from Microsoft, Wine, ReactOS or any other
  project. Write what the binary does, in your own code.
- Note what you learned in `docs/`, with the build and function it comes from.
  A number or behavior that is guessed rather than read must say so.
- When you implement a function, update its row in the matching `docs/` page.
- To refresh the progress table of the README, extract `Windows/System32` of
  the build 26300 arm64 ISO and run `tools/progress.sh <that System32>`.

## Code style

- C23: `nullptr`, `bool`, `static_assert`, `typeof`, `constexpr`, `[[attributes]]`, `= {}`, `#embed`,
  `<stdckdint.h>`. Four spaces, braces on the same line except for functions.
- Mime's own names use the `Mime`/`mime_` prefixes; Windows functions keep
  their Windows names and signatures.
- Comments are short, lowercase except identifiers, and end with a dot. Skip
  the comment when the code already says it.
- The build must stay free of warnings.

## Commits

One line in English using conventional commits, for example
`feat: add NtUserGetClipboardData`. Keep each commit focused on one change.
