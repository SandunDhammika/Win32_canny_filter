# Win32 Canny Filter (Clipboard GUI)

A native Win32 C++ desktop application that performs Canny edge detection on grayscale images.

## Features

- Win32 GUI with side-by-side panels:
  - Input image
  - Canny output image
- Clipboard image workflow:
  - Paste to input (`Ctrl+V`)
  - Paste to output (menu)
  - Copy input (`Ctrl+Shift+C`)
  - Copy output (`Ctrl+C`)
- Adjustable Canny thresholds:
  - Low threshold
  - High threshold
- Canny processing pipeline:
  - Grayscale conversion
  - Gaussian blur
  - Sobel gradient magnitude + direction
  - Non-maximum suppression
  - Hysteresis thresholding

## Project Files

- `main.cpp` - Win32 GUI + image processing implementation
- `Makefile` - GNU Make build file (MinGW/GCC style)
- `Makefile.vc` - `nmake` build file for MSVC
- `build_msvc.cmd` - helper script to locate Visual Studio Build Tools and build

## Build (MSVC - recommended on Windows)

From PowerShell in the project folder:

```powershell
cmd /c build_msvc.cmd
```

This produces:

- `canny_gui.exe`

## Build (GNU Make / MinGW)

If you have `make` + `g++` installed:

```powershell
make
```

If your system uses `mingw32-make`:

```powershell
mingw32-make
```

## Run

```powershell
.\canny_gui.exe
```

## Usage

1. Copy an image in any app.
2. In this app, use **Clipboard -> Paste to Input** (or `Ctrl+V`).
3. Set **Low Threshold** and **High Threshold**.
4. Click **Apply** or use **Process -> Run Canny** (`F5`).
5. Use **Clipboard -> Copy Output** (or `Ctrl+C`) to copy the edge image.

## Notes

- Clipboard compatibility supports both `CF_DIB` and `CF_BITMAP` paths.
- Threshold values are clamped to the range `0..255`.
- If `low > high`, values are swapped automatically.
