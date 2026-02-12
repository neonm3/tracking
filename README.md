# GevIQ24 TOP (TouchDesigner C++ TOP plugin)

This is a **TouchDesigner TOP** that can grab frames from **Matrox/Zebra GevIQ** digitizers via **MIL**.

## What you get

- A generator TOP (no inputs) called: **GevIQ24**
- Parameters:
  - **Enable**: on/off
  - **Camera Index (0..23)**: selects device number `Device Offset + Camera Index`
  - **Output Mode**: `Selected` or `Grid (24-up)`
  - **Grid Columns**: for grid mode
  - **DCF Path**: optional DCF path (leave empty to use `M_DEFAULT`)
  - **Device Offset**: add to camera index (useful if your system enumerates digitizers starting from non-zero)

## How to compile

1. Install TouchDesigner (to get the C++ TOP headers) and MIL (Matrox Imaging Library).
2. Open `BasicFilterTOP.sln` in Visual Studio (x64).
3. Add MIL include + lib paths in the project settings:
   - C/C++ -> Additional Include Directories: the folder containing `mil.h`
   - Linker -> Additional Library Directories: the folder containing MIL `.lib` files
   - Linker -> Input -> Additional Dependencies: add the required MIL libs (varies by MIL version/install)

4. Define `HAVE_MIL` in the project:
   - C/C++ -> Preprocessor -> Preprocessor Definitions: add `HAVE_MIL`

If you do **not** define `HAVE_MIL`, the project will compile but the TOP will show an error frame.

## Notes / Limitations (current scaffolding)

- Capture is implemented as a **blocking single-frame grab** (`MdigGrab`) per cook. It is the simplest starting point.
- For real-time 24-camera throughput, you’ll likely want:
  - `MdigProcess()` with ring buffers per digitizer
  - a background thread that updates CPU frames / shared textures
  - optional GPU interop (PBO / DirectX interop) to avoid CPU copies

## Typical TouchDesigner usage

- **One camera per TOP:** create 24 instances of `GevIQ24` and set Camera Index 0..23.
- **Quick overview:** set Output Mode to `Grid (24-up)`.

---
If you want, I can refactor this into a **shared capture service** (one MIL grab loop feeding all TOP instances)
so TD stays responsive even with many cameras.
