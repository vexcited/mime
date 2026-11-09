# genuine windows files

BattlEye and other identity checks compare the system files a program loads against Microsoft's. Mime serves those
checks Microsoft's bytes for a handful of files, while the image section still maps Mime's own code: Mime's
implementation is the one that runs on its kernel.

The Microsoft files are **not in this repository, nor in the built `mime` binary**. `data/genuine.manifest` lists,
per file, its Windows path, SHA-256, the Microsoft symbol-server key, and size. These are facts, not binaries.

At run time the host fetches each file from `msdl.microsoft.com` into a cache outside the guest drive
(`$XDG_CACHE_HOME/mime/genuine/<manifest digest>/…`), checks its bytes against the manifest, and installs it
atomically. The guest never sees a new file on `C:`: when a program opens a ROM system file, mimefs keeps the host's
checked copy as the file's identity (reads, sizes and data sections), while the image still maps the ROM. See
`host/genuine.c`, `MimeRomEntry.genuine` and `guest/ntos/io/mimefs.c`.

Managing the cache, host side, without the machine running:

    mime genuine status   # which files are cached
    mime genuine fetch     # download the missing ones now
    mime genuine clear     # remove the cache

A machine with no cache, or offline, keeps Mime's own files. It works; software that checks identity will reject the
file, so BattlEye-protected games will block it. The machine fetches any missing file in the background at startup.

`System32/…` entries are the identity of `\Windows\System32\…`; `SysWOW64/…` of `\Windows\SysWOW64\…`. A manifest
entry whose path Mime has no file for is fetched for nothing, so the build warns about it.

Provenance of the current hashes: image 4 of the 26H2 ESD the media creation tool fetches (26300.9457), whose
`System32` keeps the 26100.9444 files of the enablement build.

## regenerating

One line per file: `path<TAB>sha256<TAB>time-date-stamp and size-of-image in hex<TAB>size`. The symbol-server key is
the PE `TimeDateStamp` (8 hex) followed by `SizeOfImage` (hex, no padding), both from the file's optional header. Add
a line only for a file Mime also ships under that path. `tools/genmanifest.sh <esd> <image> <old manifest>` of the
research repository writes the whole list again from an image.
