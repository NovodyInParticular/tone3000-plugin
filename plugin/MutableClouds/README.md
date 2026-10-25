# Mutable Instruments Clouds (DSP only)

The granular processor from Mutable Instruments' Clouds module, used by the
plugin's Clouds effect (plugin/include/CloudsFx.h). Source:
https://github.com/pichenettes/eurorack (commit f4cfa76, `clouds/` and the
`stmlib` submodule at e3bd7c9). Only the DSP files Clouds' processor needs are
copied; firmware, drivers and UI code are left out. MIT licensed, see LICENSE.

Local patches (marked `TONE3000 patch`):

- `stmlib/stmlib.h`: `__attribute__(x)` expands to nothing on MSVC.
- `stmlib/utils/random.h`, `random.cc`: the shared random generator state is
  `thread_local`, so instances on different audio threads don't race.

Build with `TEST` defined (selects the portable C++ paths instead of ARM
assembly and STM32 headers) and `_USE_MATH_DEFINES` on MSVC; the `clouds`
target in plugin/CMakeLists.txt does both.
