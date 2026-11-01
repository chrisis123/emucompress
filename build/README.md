# emucompress GUI

This directory contains the native x64 C++ `emucompress_gui` application. The window separates Rust-based XDVDFS operations from CHDMAN disc-image operations.

## Build

1. Build the Rust library from `source\xdvdfs`:

   ```powershell
   cargo build -p xdvdfs-ffi --release
   ```

2. Open `emucompress_gui.vcxproj` in Visual Studio and build the `Release | x64` configuration.

The executable and Rust runtime DLL are written to:

```text
build\bin\x64\Release\
```

The application resolves `chdman.exe` relative to the executable and expects it at the repository root:

```text
J:\Emuroms\CHD_TMP\chdman.exe
```

## XDVDFS operations

Drop an input file onto the window, then use the XDVDFS row:

- `ISO to XISO` calls the Rust XDVDFS pack implementation.
- `XISO to folder` extracts an `.xiso.iso` image into a folder beside the input.
- `XISO to ISO` extracts an `.xiso.iso` image and creates a standard ISO9660 `.iso` file beside the input using Windows IMAPI2.

## CHDMAN operations

Drop a file onto the window, then use the CHDMAN row. Output names are generated automatically beside the input by replacing its extension.

- `Create CD CHD` accepts `.cue`, `.gdi`, or `.iso` and invokes:

  ```text
  chdman.exe createcd -i <input> -o <output.chd>
  ```

- `Create DVD CHD` accepts `.iso` and invokes:

  ```text
  chdman.exe createdvd -i <input.iso> -o <output.chd>
  ```

- `Extract CD` accepts `.chd` and writes a selected `.cue` output:

  ```text
  chdman.exe extractcd -i <input.chd> -o <output.cue>
  ```

- `Extract CD to ISO` accepts `.chd`, creates a temporary `.cue`, writes the selected `.iso`, and deletes the temporary CUE file:

  ```text
  chdman.exe extractcd -i <input.chd> -o <temporary.cue> -ob <output.iso>
  ```

- `Extract DVD` accepts `.chd` and writes a selected `.iso` output:

  ```text
  chdman.exe extractdvd -i <input.chd> -o <output.iso>
  ```

The GUI operates on one selected input at a time. The batch files in the repository recurse through folders; the GUI equivalent is to drop each required input file onto the window.

Conversions run in the background and the progress bar displays byte-based progress and an estimated time remaining.
