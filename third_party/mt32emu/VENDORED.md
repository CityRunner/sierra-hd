# mt32emu, vendored

- Upstream: https://github.com/munt/munt, directory `mt32emu/` (the library
  of the Munt project)
- Version: libmt32emu 2.8.3, tag `libmt32emu_2_8_3`, commit
  `3b05ec276f9e605af86b0eaef7f5eda43477a31f` (2026-06-06, "Reworked sanity
  checks for short MIDI messages"), the latest library release when vendored
  on 2026-10-28. (Releases before 2.8 were tagged `mt32emu_x_y_z`; the
  reference ScummVM carries 2.7.0.)
- Licence: LGPL 2.1 or later (`COPYING.LESSER.txt`; the notice in every
  source file). `sha1/` is by Micael Hildenborg under the 3-clause BSD
  licence, in the notice of `sha1.cpp` and `sha1.h`; `sha1-LICENSE.txt` is
  that notice (lines 1-26 of `sha1.cpp`, unchanged), for the release
  archives.

Copied unchanged, from upstream's `mt32emu/src/` to `mt32emu/` here (so that
the headers are reached as `<mt32emu/Synth.h>`, as in an installed
library), with their SHA-256:

| File | Upstream (`mt32emu/`) | SHA-256 |
|---|---|---|
| `COPYING.LESSER.txt` | `COPYING.LESSER.txt` | `36b6d3fa47916943fd5fec313c584784946047ec1337a78b440e5992cb595f89` |
| `AUTHORS.txt` | `AUTHORS.txt` | `28be72f7ebc5ee6cd98896e8a37855b50248d9b5167269b047d71d0fd03cdc9a` |
| `mt32emu/Analog.cpp` | `src/Analog.cpp` | `3c6dc409afea053effbb3457b0c545f86bbffd1a4bdf720bd957ec71d9640964` |
| `mt32emu/Analog.h` | `src/Analog.h` | `105e59d100c03f3f8e8b0a0b161d3b7641e5f54e588abceea9dbc32fc7f1fa73` |
| `mt32emu/BReverbModel.cpp` | `src/BReverbModel.cpp` | `5d0aab5b0e271c261d6bfe7267613d3763dd93afca43e084c67c0b99e2578352` |
| `mt32emu/BReverbModel.h` | `src/BReverbModel.h` | `ae08d9d0412a75ddc013256d0d289df60ea10306f87aadc5ca1474d83835c89e` |
| `mt32emu/Display.cpp` | `src/Display.cpp` | `033f67cadd75311a1205bfcec307ec8348a70c329f1d041f504a905c6a9cf0c2` |
| `mt32emu/Display.h` | `src/Display.h` | `7faa1532fc2351c4509f15bf3e26e92d3868e977f1c11c7b04bd75e70fcb1526` |
| `mt32emu/Enumerations.h` | `src/Enumerations.h` | `85c95d52a62e91b9312ce7595007d45e50afc503fee40629df43a5ba29a9b66c` |
| `mt32emu/File.cpp` | `src/File.cpp` | `a86cb134b1a23c0adac1a3841cf6d540aebd8a2cfc407b943dfc31c167c594c9` |
| `mt32emu/File.h` | `src/File.h` | `09c3efed1e776cdef593eb9376a7a799bbc4462ccb76ad17b120ed171c74176d` |
| `mt32emu/LA32FloatWaveGenerator.cpp` | `src/LA32FloatWaveGenerator.cpp` | `2b5635731653b6a0f8865e4381ba945291f15eaf2a0bbf78b8d42e6df1cbc02a` |
| `mt32emu/LA32FloatWaveGenerator.h` | `src/LA32FloatWaveGenerator.h` | `4e4a4b1f12f90a587cff160ce7c66711d7e74b388959cb2519f2430bbea78e60` |
| `mt32emu/LA32Ramp.cpp` | `src/LA32Ramp.cpp` | `89bf8e756f7da5b741697a7a77c3936431ee98f1b7f73d4b858a66dca7a63199` |
| `mt32emu/LA32Ramp.h` | `src/LA32Ramp.h` | `2045a3fc4efa7873d9e19c9e2c1fcf9bc975d8f7ecce2a797f907513c7dcc05a` |
| `mt32emu/LA32WaveGenerator.cpp` | `src/LA32WaveGenerator.cpp` | `3f49b2b87ebab514b370dcff2415ff81e7a12dcfeef9886aada87de655b9a27e` |
| `mt32emu/LA32WaveGenerator.h` | `src/LA32WaveGenerator.h` | `6513a7d7ab100707761b555b02889f80b24657ad43f8c087bd538101e082d88a` |
| `mt32emu/MemoryRegion.h` | `src/MemoryRegion.h` | `513fefab2292a316ac3b4bb74308a4d1e57ccef08f72d0b9ca601cf2ca1e03b7` |
| `mt32emu/MidiEventQueue.h` | `src/MidiEventQueue.h` | `cfb764c7a513f4fd284d05726bb25cc4233f2228deeb3e8596982fc88a6fb0c7` |
| `mt32emu/Part.cpp` | `src/Part.cpp` | `ecafec208e68d602a50ae87bef0df27aff8f1f2467c4b91434139f459792846c` |
| `mt32emu/Part.h` | `src/Part.h` | `34a8814f3fcd5f22ea1a05ccc25e3b652804bae48b600b67729ca222ef6ac6c7` |
| `mt32emu/Partial.cpp` | `src/Partial.cpp` | `9a76e57a9fa31623912b9f0eaa97bb7149e21f64e3abef7804a4e3e25add1391` |
| `mt32emu/Partial.h` | `src/Partial.h` | `a3a50264074acb53b147a1fa64563adca8af724be506190fa3befc0b09b817c8` |
| `mt32emu/PartialManager.cpp` | `src/PartialManager.cpp` | `cddbc8fdd16433f6a884e51bdbcc5cd9216aa3fa6dcf99e982a7e61ce278bc8a` |
| `mt32emu/PartialManager.h` | `src/PartialManager.h` | `76fcc12aeea25799cacd71a7fdab25365d12d306f52eb8e27738d8c469e0d19d` |
| `mt32emu/Poly.cpp` | `src/Poly.cpp` | `3b8f125802b6ec34aa05ab1c218e70e59bb02f746f7bff9defe3a237e64514eb` |
| `mt32emu/Poly.h` | `src/Poly.h` | `7c4f9bd1f58208de97cd6b58781d3943d2a491a17746fd994c8c6c6eef5a200f` |
| `mt32emu/ROMInfo.cpp` | `src/ROMInfo.cpp` | `bad40b1e121e4fd678bcdb3fb8476c7252de287d75c0fad6e7767e4cd8fd2ebd` |
| `mt32emu/ROMInfo.h` | `src/ROMInfo.h` | `a10f8f164e12801d7ba8224e51f4fecf22996d70e58828d4127e4bd311b7e2f4` |
| `mt32emu/Structures.h` | `src/Structures.h` | `3573caa7019ff1c6cfb7b8a2442d20e808b484939e151251904921ed337b08a9` |
| `mt32emu/Synth.cpp` | `src/Synth.cpp` | `0364c8b9f4e502dcfc7932f702d821aa7806925aa0e748dfac6a6038fc78eb46` |
| `mt32emu/Synth.h` | `src/Synth.h` | `7f2aa4ae934ecb2b3fc4f3f30d3b833b7575e9899054b98cfcade1d343357a6e` |
| `mt32emu/SysexBuilder.h` | `src/SysexBuilder.h` | `073d08a92fba2f29ff30c503610fb10de83501654be3bc3268b38b121db14897` |
| `mt32emu/TVA.cpp` | `src/TVA.cpp` | `a34a9612ad835d544f3264f4b1629676aace1579691ed20dfdf7685f7450f818` |
| `mt32emu/TVA.h` | `src/TVA.h` | `1db0189ad6485840e5467348b86e8245148a74299cec90544d21ce94ef21bd0c` |
| `mt32emu/TVF.cpp` | `src/TVF.cpp` | `6efa9b34b9a80aacb92a4c44653da7bbc4eb9f22c0764238480dc0dacb2a95e0` |
| `mt32emu/TVF.h` | `src/TVF.h` | `2ea2ceadffcd18135686855b8f465b2f0179520d03173436f7334ee8fe1e1452` |
| `mt32emu/TVP.cpp` | `src/TVP.cpp` | `b49e742a0bac8b1d40c9238368da53117e61cfb2ba675544f25d91a4e86fc488` |
| `mt32emu/TVP.h` | `src/TVP.h` | `312166e0579a95aa23230e473c0803725b0e4754c047167091d977e4f268efa9` |
| `mt32emu/Tables.cpp` | `src/Tables.cpp` | `f502e9a4bbaee5cb543283c05ce08b72f51fd55ab07168d1c100adcd321c4118` |
| `mt32emu/Tables.h` | `src/Tables.h` | `a1d738e44293f31d99c447d30d7728b05968264030c1f8bcfaae0f743ebd9fa9` |
| `mt32emu/Types.h` | `src/Types.h` | `96e4ec0fdd9e3d6642a3b96d0b51068ce00cc5c16572e2f76837229881ff52a7` |
| `mt32emu/globals.h` | `src/globals.h` | `83d50c4378116f493c2de211d770891a98d6e0526e872ce9668ca6e538343f5f` |
| `mt32emu/internals.h` | `src/internals.h` | `5246be828ef0243a085528ff701047754b88d0e2cf40456110e5b0f2cf575d57` |
| `mt32emu/mmath.h` | `src/mmath.h` | `9097511f0ab95857b41754025a8ebf12e48e640866e88d43f9b620eabbd0a817` |
| `mt32emu/sha1/sha1.cpp` | `src/sha1/sha1.cpp` | `096215a0c16920cdbf7d13cce7d3d69aa713295da983f35a7b61ae3261a5a926` |
| `mt32emu/sha1/sha1.h` | `src/sha1/sha1.h` | `b0f16ca0edddc5f2ff1f44a9f4aedc56236617ad0a556585e35af924faa4fc1c` |

Added here:

| File | SHA-256 | What |
|---|---|---|
| `config/config.h` | `6dff34fa4f0ffa4855d3c6dc1fae4c263dd6b635092ea131f337e7424e0f1248` | written for this project in place of the `config.h` that upstream's CMake generates from `src/config.h.in`: version 2.8.3, a static library with the C++ API only (`MT32EMU_EXPORTS_TYPE 0`), no version tagging |
| `sha1-LICENSE.txt` | `1d773c3b9731d2914567f7345174b149b4cd14fdc798e78cc48148240a2deab4` | the BSD notice of `sha1/sha1.cpp`, extracted |

Left out: upstream's build files (`CMakeLists.txt`, `cmake/`, `config.h.in`,
`mt32emu.pc.in`), the umbrella header `mt32emu.h` (it includes the
resampler), the file streams (`FileStream.*`: the program reads the ROM
files itself, in app), the MIDI stream parser (`MidiStreamParser.*`), the
sample rate converter and its resamplers (`SampleRateConverter.*`,
`srchelper/`: the mixer resamples), version tagging (`VersionTagging.*`),
the C and plugin interfaces (`c_interface/`), the unit tests (`test/`) and
the documentation (`README.md`, `NEWS.txt`, `TODO.txt`, `info.txt`,
`COPYING.txt`, which is the GPL v2 that upstream ships for reference).

Built by `cmake/SciThirdParty.cmake` as the static C++20 library
`sci_mt32emu`, with its warnings silenced. Only sci-audio links it, and only
`src/sci-audio/src/mt32.cpp` includes its headers.

Notes (docs/ASSUMPTIONS.md, "Milestone 7: mt32emu"):
- Floating point: the integer renderer (`RendererType_BIT16S`, the default)
  is used, with the coarse analogue mode (32000 Hz output). Floating point
  is then used only to build mt32emu's tables at start-up (`Tables.cpp`,
  with `log`, `exp2` and `sin`; the pan factors in `Partial.cpp`), to turn
  the output gains into integer factors and to convert the MIDI cable's
  transfer time to samples (`double`); the float renderer (`LA32FloatWaveGenerator.cpp`) is compiled
  but not selected.
- `TVP.cpp` calls the C library's `rand()` for a small jitter of the pitch
  timer (as on the real unit). The output is therefore a function of the
  ROMs, the input and `rand()`'s state.
- Global state: the tables (`Tables::getInstance`, built once and then
  read-only) and `rand()`'s state; everything else is in the `Synth`.
- No threads, no file I/O in the files built (the default report handler
  prints to stdout; sci-audio installs its own, which prints nothing).
- MSVC-specific code: only `Structures.h` (`__declspec(align(1))` and
  `#pragma pack` under `_MSC_VER`, which clang-cl accepts) and `globals.h`
  (`__declspec(dllexport)` for shared builds, not used here).

To update, copy the same files from the new tag, check the SHA-256, compare
`src/config.h.in` with `config/config.h` and record the new tag here, in
`THIRD_PARTY.md` and in `docs/ASSUMPTIONS.md`.
