# genuine windows files

BattlEye and other identity checks compare the system files a program loads against Microsoft's. Mime serves those
checks Microsoft's bytes for a handful of files, while the image section still maps Mime's own code: Mime's
implementation is the one that runs on its kernel.

The Microsoft files are **not in this repository, nor in the built `mime` binary**. `data/genuine.manifest` lists,
per file, its Windows path, SHA-256, the Microsoft symbol-server key, and size, all of the Windows build its first line
names. These are facts, not binaries.

Mime runs nothing without them. `mime setup`, and `mime up` or `mime run` when it was not done yet, download the
missing ones from `msdl.microsoft.com` into a cache outside the guest drive (`$XDG_CACHE_HOME/mime/genuine/<manifest
digest>/`), check each against the manifest, and install it atomically. A failed download stops there with the
reason, in the terminal or in an error box for a program started from the desktop. The machine opens every file of
the set when it starts, and refuses to start without one.

The guest never sees a new file on `C:`: when a program reads a ROM system file the manifest lists, mimefs reads the
host's copy, and its size, listings and data sections are that copy's, while the image still maps the ROM. See
`host/genuine.c`, `MimeRomEntry.genuine` and `guest/ntos/io/mimefs.c`.

`System32/…` entries are the identity of `\Windows\System32\…`; `SysWOW64/…` of `\Windows\SysWOW64\…`. A manifest
entry whose path Mime has no file for is fetched for nothing, so the build warns about it.

Provenance of the current hashes: image 4 of the 26H2 ESD the media creation tool fetches (26300.9457), whose
`System32` keeps the 26100.9444 files of the enablement build.

## regenerating

The first line is `# build <build>.<revision>`, then one line per file: `path<TAB>sha256<TAB>time-date-stamp and
size-of-image in hex<TAB>size`. The symbol-server key is the PE `TimeDateStamp` (8 hex) followed by `SizeOfImage`
(hex, no padding), both from the file's optional header. Add a line only for a file Mime also ships under that path.
`tools/genmanifest.sh <esd> <image> <old manifest>` of the research repository writes the whole list again from an
image.
