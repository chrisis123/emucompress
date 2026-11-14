# Third-party notices

emucompress is distributed together with the third-party components listed
below. Each one stays under its own license. The complete license texts ship in
the `LICENSES` folder (next to `emucompress_gui.exe` in a release, and at the
root of the source repository).

The emucompress GUI itself is licensed under **GPL-2.0-or-later**; see
[`LICENSE`](LICENSE).

## Components in a release folder

| File | Component | License | Copyright / upstream | License texts |
| --- | --- | --- | --- | --- |
| `emucompress_gui.exe` | emucompress GUI | GPL-2.0-or-later | Copyright (C) 2026 chrisis123 and emucompress contributors | `LICENSE`, `LICENSES/GPL-2.0-or-later.txt` |
| `chdman.dll` | chdman, from MAME | BSD-3-Clause (per file, see below) | Copyright MAMEdev and contributors, mainly Aaron Giles; https://github.com/mamedev/mame | `LICENSES/third-party/mame-chdman/` |
| `xdvdfs_ffi.dll` | xdvdfs and its Rust dependencies | MIT, plus the crate licenses (MIT, Apache-2.0, BSD-3-Clause, Zlib, Unicode-3.0, Unlicense) | Copyright 2023 antangelo; https://github.com/antangelo/xdvdfs | `LICENSES/third-party/xdvdfs/` |
| `DolphinRVZ.dll` | Dolphin disc I/O, built from the Dolphin source tree | GPL-2.0-or-later, with bundled libraries under their own licenses (see below) | Copyright Dolphin Emulator Project; https://github.com/dolphin-emu/dolphin | `LICENSES/third-party/dolphin/` |
| `msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` | Microsoft Visual C++ runtime | Microsoft redistributable terms | Copyright Microsoft Corporation | Not covered by the licenses in this folder; redistributed under the Visual Studio / Visual C++ Redistributable license terms |

### ZArchive and Zstandard (compiled into emucompress_gui.exe)

The Xbox 360 ZAR support (ISO to ZAR, ZAR to XISO) uses these libraries,
which are compiled directly into `emucompress_gui.exe`. Their sources are in
the repository and are unmodified:

| Library | License | Text |
| --- | --- | --- |
| ZArchive (Exzap), `source/zarchive` | MIT No Attribution (MIT-0), Copyright 2022 Exzap; https://github.com/Exzap/ZArchive | `zarchive/ZArchive-MIT-0.txt` |
| sha-2 (Alain Mosnier), `sha_256.c` / `sha_256.h` inside ZArchive | Unlicense (public domain) or 0BSD, at your option; https://github.com/amosnier/sha-2 | `zarchive/sha-2-LICENSE.txt` |
| Zstandard (Meta Platforms, Inc.), `source/mame/3rdparty/zstd` | BSD-3-Clause (used under the BSD option of its BSD/GPL-2.0 dual license) | `zarchive/zstd-BSD-3-Clause.txt` |

The paths in the last column are relative to `LICENSES/third-party/`. The
ZAR format specification and the XGDTool and Xenia Canary projects were only
consulted as references; no code from them is included.

### chdman.dll (MAME)

`chdman.dll` is built from `source/mame/src/tools/chdman_dll.cpp` and
`source/mame/src/tools/chdman.cpp`. Those files, `src/lib/util` and `src/osd`
are `BSD-3-Clause` (some utility files are `MIT` or Public Domain, as stated in
each file's `license:` header). MAME as a whole is distributed under the GPL,
but no GPL-only MAME file is part of `chdman.dll`. MAME is a registered
trademark of Gregory Ember.

Libraries statically linked into `chdman.dll`:

| Library | License | Text |
| --- | --- | --- |
| libFLAC (Josh Coalson, Xiph.Org Foundation) | BSD-3-Clause (Xiph) | `flac-Xiph.txt` |
| zlib (Jean-loup Gailly, Mark Adler) | Zlib | `zlib.txt` |
| Zstandard (Meta Platforms, Inc.) | BSD-3-Clause (used under the BSD option of its BSD/GPL-2.0 dual license) | `zstd.txt` |
| LZMA SDK (Igor Pavlov) | Public Domain | Stated in the LZMA SDK sources (`source/mame/3rdparty/lzma/DOC/7zC.txt`) |
| Expat | MIT | `expat.txt` |
| utf8proc | MIT, plus Unicode data terms | `utf8proc.txt` |

The upstream MAME copyright and license overview is in
`LICENSES/third-party/mame-chdman/MAME-COPYING.txt`. The license texts it
refers to are in `source/mame/docs/legal`.

### xdvdfs_ffi.dll (xdvdfs)

`xdvdfs_ffi.dll` is built from `source/xdvdfs` (crate `xdvdfs-ffi` with
`xdvdfs-core`), which is `MIT` licensed. The name, version, license expression
and license text of every Rust crate it was built from is listed in
`LICENSES/third-party/xdvdfs/rust-crates-LICENSES.txt`. Crates that are only
used at build time (procedural macros) are listed there too. `bitbybit` and
`proc-bitfield-macros` do not ship a license file; they are build-time macro
crates licensed `MIT` and `MIT OR Apache-2.0` respectively according to their
`Cargo.toml`.

### DolphinRVZ.dll (Dolphin)

`DolphinRVZ.dll` is built from the Dolphin source tree in `source/dolphin`.
Most Dolphin code is `GPL-2.0-or-later`; parts are derived from other projects
under compatible licenses, as marked in the per-file SPDX tags (see
`LICENSES/third-party/dolphin/Dolphin-COPYING.txt`). The additions made for
emucompress (`Source/Core/DiscIO/DolphinRVZ.cpp`, `DolphinRVZ.h` and the
corresponding `CMakeLists.txt` entry) are also `GPL-2.0-or-later`.

Libraries from Dolphin's `Externals` directory that may be linked into the DLL,
with their licenses (texts in `LICENSES/third-party/dolphin/<name>.txt`):

| Library | License |
| --- | --- |
| bzip2 | bzip2 license (BSD-style) |
| liblzma (XZ Utils) | 0BSD / public domain |
| lz4 | BSD-2-Clause (library) |
| zstd | BSD-3-Clause |
| zlib-ng | Zlib |
| minizip-ng | Zlib |
| fmt | MIT |
| xxHash | BSD-2-Clause |
| mbedTLS | Apache-2.0 OR GPL-2.0-or-later |
| pugixml | MIT |
| ENet | MIT |
| ed25519 | Zlib-style |
| FatFs | FatFs license (BSD-style) |
| LZO | GPL-2.0-or-later |
| cpp-optparse | MIT |
| picojson | BSD-2-Clause |
| expr | MIT |
| Dear ImGui | MIT |
| ImPlot | MIT |
| SFML | Zlib |
| libcurl | curl license (MIT-style) |
| libspng | BSD-2-Clause |
| miniupnpc | BSD-3-Clause |
| mGBA | MPL-2.0 |
| cubeb | ISC |
| rcheevos | MIT |
| HIDAPI | BSD-3-Clause / GPL-3.0 / original license |
| libusb | LGPL-2.1-or-later |
| SDL | Zlib |
| SPIRV-Cross | Apache-2.0 |
| glslang | BSD-3-Clause, with Apache-2.0 and other parts |
| cpp-ipc | MIT |
| Windows Implementation Library (wil) | MIT |
| watcher | MIT |
| Vulkan-Headers | Apache-2.0 / MIT |
| Vulkan Memory Allocator | MIT |
| TinyGLTF | MIT |
| Bochs disassembler | LGPL-2.1-or-later (see `LICENSES/LGPL-2.1.txt`) |
| FreeSurround | GPL-2.0-or-later (see `LICENSES/GPL-2.0-or-later.txt`) |

Where a library offers several licenses, a license permitted for the combined
work (GPL-2.0-or-later) is used. The statically linked LGPL-2.1 libraries
(libusb, the Bochs disassembler) can be replaced by modified versions by
rebuilding `DolphinRVZ.dll` from the provided source.

## Source code (GPL / LGPL section 3)

`emucompress_gui.exe` and `DolphinRVZ.dll` are covered by the GPL. The
corresponding source code, including the emucompress changes to Dolphin and the
build files needed to rebuild the binaries, is available at

> https://github.com/chrisis123/emucompress2

The Dolphin and MAME source trees used for the build are in `source/dolphin`
and `source/mame`; unmodified upstream versions are at
https://github.com/dolphin-emu/dolphin and https://github.com/mamedev/mame.

If you received a binary release without the source code, you may request it
for at least three years after you received the binary, for a charge no more
than the cost of physically performing the distribution, by opening an issue at
the repository above. Anyone who redistributes these binaries must pass on this
notice and the license files, and either provide the source with the binaries
or pass on this offer.

## Disclaimer

All software is provided "as is", without warranty of any kind. The emucompress
authors are not affiliated with MAMEdev, the Dolphin Emulator Project,
antangelo, Microsoft, Nintendo or Xbox. Trademarks are property of their
owners. Use emucompress only with disc images you are legally entitled to copy.
