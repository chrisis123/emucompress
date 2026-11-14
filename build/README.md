# emucompress GUI

`emucompress_gui.exe` is a Windows x64 utility for converting and extracting disc-image files. It brings three toolsets together in one drag-and-drop or file-picker interface:

- Xbox XDVDFS/XISO packing and extraction, implemented by the Rust XDVDFS library, plus Xbox 360 ISO to ZAR (ZArchive) conversion for Xenia.
- CD/DVD CHD creation and extraction, provided by the MAME CHDMAN library.
- GameCube/Wii ISO and RVZ conversion, provided by Dolphin DiscIO.

The GUI starts each conversion in the background so the window remains responsive. Build dependencies are loaded from DLLs beside the executable; keep those runtime DLLs in the same folder when moving the portable application.

## Using the application

1. Start `emucompress_gui.exe`.
2. Provide input files using either `Choose files...` or drag and drop. The picker accepts multiple files; drag-and-drop intentionally selects only the first dropped file.
3. Choose the operation button matching the selected inputs. The operation applies to every file selected in the picker.
4. Optionally select an output folder for that operation group. If no folder is selected, outputs are placed beside their corresponding input files.
5. Watch the status line and progress bar. Batch items are processed sequentially; incompatible files and failed conversions are recorded, and processing continues with the remaining files.
6. To stop work, click `Cancel operation` and confirm. RVZ/ISO conversions and ISO to ZAR stop cooperatively (ZAR to XISO can be stopped while extracting). XDVDFS packing and CHDMAN do not expose safe cancellation, so the active file finishes and any remaining batch files are skipped.

The three output-folder selections are independent and remain in effect until changed or the application closes. Output filenames are derived automatically from the input name. The GUI checks for existing outputs in some conversion paths; avoid choosing a destination that already contains files with the generated names. If a batch contains inputs with duplicate basenames and a shared output directory, later operations may encounter an existing output.

`README` in the window header opens this guide using the system's default Markdown or text handler. The Release build places `README.md` beside the executable for portable use. `About` in the header provides a short program overview. Hovering over any enabled button shows a tooltip explaining what it does; buttons that are disabled while a job is running do not show tooltips.

The window uses a custom-drawn, DPI-aware layout (per-monitor scaling) and follows the system light/dark app setting. Each tool (Xbox, CD / DVD, GameCube / Wii) is a card showing its operations, its current output folder, and `Change...` / `Reset` buttons (`Reset` returns to writing beside the input). While a conversion runs, the operation, file-picker, and folder buttons are disabled and files dropped onto the window are ignored. Keyboard navigation with Tab, Space, and Enter is supported.

## Build

1. Build the Rust library from `source\xdvdfs`:

   ```powershell
   cargo build -p xdvdfs-ffi --release
   ```

2. Fetch Dolphin's submodules, configure its x64 Visual Studio build, and build only the standalone `dolphin-rvz` shared-library target (do not build the default `ALL_BUILD` target):

   ```powershell
   git -C source\dolphin submodule update --init --recursive
   cmake -S source\dolphin -B source\dolphin\build-x64 -G "Visual Studio 18 2026" -A x64 -DENABLE_QT=OFF -DENABLE_NOGUI=OFF -DENABLE_CLI_TOOL=OFF -DENABLE_AUTOUPDATE=OFF -DUSE_SYSTEM_LIBS=OFF -DENABLE_TESTS=OFF -DENABLE_CUBEB=OFF -DENABLE_ANALYTICS=OFF -DENABLE_VULKAN=OFF -DENABLE_SDL=OFF -DENABLE_LLVM=OFF -DUSE_MGBA=OFF -DUSE_RETRO_ACHIEVEMENTS=OFF -DUSE_DISCORD_PRESENCE=OFF -DUSE_UPNP=OFF
   cmake --build source\dolphin\build-x64 --config Release --target dolphin-rvz
   ```

	  This builds `DolphinRVZ.dll` and only the libraries required by its DiscIO conversion code; it does not build the Dolphin application or DolphinTool executable. The `Release | x64` GUI build copies it and its required MSVC runtime DLLs beside the executable.

3. Build the CHDMAN shared library from the trimmed MAME tree in `source\mame`. It contains only the sources `chdman.dll` needs (`src/tools`, `src/lib/util`, `src/osd` and the `3rdparty` libraries zlib, zstd, FLAC, LZMA, expat, utf8proc, pugixml, asio, nanosvg and the AES helpers) plus the pre-generated Visual Studio projects:

   ```powershell
   msbuild source\mame\build\vs2022\projects\windows\mame\vs2022\chdman_dll.vcxproj /p:Configuration=Release /p:Platform=x64
   ```

   The result is `source\mame\build\vs2022\vs2022\bin\x64\Release\chdman.dll`, which the GUI build copies beside the executable. The full MAME source is at https://github.com/mamedev/mame.

4. Build `emucompress_gui.vcxproj` in Visual Studio using the `Release | x64` configuration. The project's post-build step stages `README.md`, `DolphinRVZ.dll`, its MSVC runtime dependencies, and any available conversion DLLs beside `emucompress_gui.exe`.

The executable and Rust runtime DLL are written to:

```text
build\bin\x64\Release\
```

The application loads `chdman.dll` from the same directory as the executable. This DLL is built from the checked-out MAME CHDMAN source and is independently distributable beside the GUI:

```text
build\bin\x64\Release\chdman.dll
```

## Xbox XDVDFS/XISO operations

The XDVDFS row works with Xbox disc-image files. Select one or multiple input files and choose the operation; unsupported input types are skipped in a batch.

- `ISO to XISO` accepts an ISO image and packs the XDVDFS filesystem into an `.xiso.iso` output.
- `XISO to folder` accepts an `.xiso.iso` image and extracts its filesystem to a folder derived from the input name.
- `XISO to ISO` accepts an `.xiso.iso` image and repacks it as a native XDVDFS image with an `.iso` extension. The output is padded to at least dual-layer DVD capacity.
- The Xbox card's `Change...` button changes the destination directory for these operations; when unset, the output is beside the input.

### Xbox 360 ZAR operations

[ZAR](https://github.com/Exzap/ZArchive) (ZArchive) is the compressed archive format that Xenia / Xenia Canary can mount directly. A ZAR stores the **files** of a disc (zstd-compressed in 64 KiB blocks) rather than the disc image itself.

- `ISO to ZAR` accepts an Xbox / Xbox 360 `.iso` (or `.xiso.iso`) and writes a `.zar` archive of all its files and folders. Plain XISO and the XGD1, XGD2 and XGD3 disc layouts are detected automatically. The archive root is the disc root, so a bootable Xbox 360 game has `default.xex` at the top level. The GUI warns if it is missing; Xenia only runs Xbox 360 titles, not original Xbox games. This operation can be cancelled and removes an incomplete output.
- `ZAR to XISO` accepts a `.zar` and rebuilds a standard `.xiso.iso` image from its files. The files are first extracted to a temporary `*.extract.tmp` folder next to the output (so free disk space for the full game is needed while it runs), the XISO is built from it, and the temporary folder is deleted afterwards. Cancelling is possible during extraction but not while the XISO is being built.
- Both operations ask for confirmation first and refuse to overwrite an existing output file. File names inside an archive are validated, so an archive with unsafe names (for example `..`, drive letters or device names such as `CON`) is rejected.

**The conversion is reversible, but the output is not byte-identical to the original input.** ZAR keeps only the files, so ISO to ZAR to XISO restores every file with identical contents, but the rebuilt image will differ from the original ISO: the video partition, padding, sector layout, security sectors and XGD offset are not preserved, and the XDVDFS layout is generated anew. Keep the original ISO if you need a verbatim copy (for example for checksum-based verification against a dump database). The GUI shows this notice before each ZAR conversion and in the completion message, and the buttons' tooltips repeat it.

ZAR support is built into `emucompress_gui.exe` (ZArchive and Zstandard are compiled in); disc reading uses the new streaming `xdvdfs_walk` function in `xdvdfs_ffi.dll`.

## CHD operations

The CHDMAN row creates or extracts MAME CHD images. Output names use the input basename and the operation's output extension. The CD / DVD card's `Change...` button selects a destination; otherwise outputs are written beside the input.

- `Create CD CHD` accepts `.cue`, `.gdi`, or `.iso` input. It creates a `.chd` using CHDMAN's `createcd` operation:

  ```text
	chdman.dll createcd -i <input> -o <output.chd>
  ```

- `Create DVD CHD` accepts `.iso` input and creates a `.chd` using `createdvd`:

  ```text
	chdman.dll createdvd -i <input.iso> -o <output.chd>
  ```

- `Extract CD` accepts `.chd` input and creates a `.cue` descriptor (and associated track data as handled by CHDMAN):

  ```text
	chdman.dll extractcd -i <input.chd> -o <output.cue>
  ```

- `Extract CD to ISO` accepts `.chd`, uses a temporary `.cue` descriptor, writes the final `.iso`, and deletes the temporary descriptor after a successful extraction. Both files are created in the selected output folder while the operation runs:

  ```text
	chdman.dll extractcd -i <input.chd> -o <temporary.cue> -ob <output.iso>
  ```

- `Extract DVD` accepts `.chd` input and creates an `.iso` using `extractdvd`:

  ```text
	chdman.dll extractdvd -i <input.chd> -o <output.iso>
  ```

- `Extract CD` and `Extract DVD` both accept CHD input, but use different CHDMAN operations and produce different output structures. Choose the action matching the CHD's disc type.
- `Extract CD to ISO` is convenient when a single ISO output is needed instead of the CUE/track representation.

## GameCube/Wii RVZ operations

The Dolphin DLL is produced at `source\dolphin\build-x64\Binaries\DolphinRVZ.dll`; the GUI project's post-build step stages it and its MSVC runtime dependencies beside `emucompress_gui.exe`. Dolphin's other linked dependencies are static. Keep all staged DLLs beside the executable when moving the portable application.

- `ISO to RVZ` accepts a GameCube/Wii `.iso` and invokes Dolphin's native DiscIO conversion with RVZ, 128 KiB chunks, and Zstandard level 5. Scrubbing is disabled; the conversion retains unused/junk-sector data rather than replacing it with zeros.
- `RVZ to ISO` accepts `.rvz` and converts it back to `.iso`. With input data retained during RVZ creation, converting the RVZ back is intended to reproduce the source image bytes.
- The output is written beside the input by default; the GameCube / Wii card's `Change...` button can select another location. Existing target files are rejected for RVZ conversions rather than overwritten.
- Dolphin's progress callback updates the shared progress bar. The status line displays the current conversion activity and estimated time remaining.
- `DolphinRVZ.dll` uses Dolphin's GPL-2.0-or-later DiscIO source. When distributing the DLL, comply with the GPL, provide the corresponding source and build changes, and include the applicable license and notices. A portable bundle must include the DLL's runtime dependencies beside the GUI as well.

The MAME-derived CHDMAN source remains under its original BSD-3-Clause license. Keep the MAME license and attribution files when distributing `chdman.dll`.

## Batch processing, progress, and errors

The file picker can select multiple files at once. After selecting files, press one operation button to apply that same operation to the entire selection. Batch conversions are sequential, not parallel, so progress and status describe the current file. Files with an incompatible extension or files whose conversion fails are skipped/recorded and do not stop the rest of the batch. At the end, a summary reports the number of successful conversions and lists failures (up to eight details in the dialog).

Drag-and-drop support remains available and selects one input per drop. If a file is dropped after using the multi-select picker, the dropped file becomes the active single-file selection. Choosing new files in the picker replaces the previous selection.

Conversions run in a background thread. Progress is displayed in the shared bar and status line; ETA is an estimate based on available byte/progress information and may vary by format. Output names are generated from each input, and the application reports conversion errors in the status line or batch summary. Batch processing does not automatically retry failures or overwrite existing outputs.

`Cancel operation` always asks for confirmation. For RVZ/ISO conversions the active conversion checks the cancel request through Dolphin's progress callback, removes an incomplete output, and ends the batch. ISO to ZAR (and the extraction phase of ZAR to XISO) behaves the same way and also removes its partial output and temporary folder. For XDVDFS and CHDMAN operations the current backend call cannot be interrupted safely; after confirmation that current file is allowed to finish, and the GUI skips the rest of the batch. Declining the confirmation leaves the current operation and batch unchanged.

## Portable runtime files

Keep the executable and the DLLs staged into `build\bin\x64\Release\` together. The Rust XDVDFS DLL is a direct dependency; CHDMAN and DolphinRVZ are loaded dynamically for their respective operations. The MSVC runtime DLLs required by DolphinRVZ are also staged there. `README.md` is copied into the same folder and opened by the header `README` button.

## License

emucompress is free software under the **GNU GPL, version 2 or (at your option) any later version** (see `LICENSE`). It ships with MAME chdman (BSD-3-Clause), xdvdfs (MIT), ZArchive (MIT-0, with its sha-2 code under Unlicense/0BSD), Zstandard (BSD-3-Clause) and Dolphin's disc code (GPL-2.0-or-later), each under its own license. `THIRD_PARTY_NOTICES.md` lists every bundled component, its copyright notice and where to get the corresponding source, and the `LICENSES` folder holds the full license texts. The build copies `LICENSE`, `THIRD_PARTY_NOTICES.md` and `LICENSES\` from the repository root next to the executable; keep them with the binaries when you redistribute them. The build fails if they are missing from the repository.
