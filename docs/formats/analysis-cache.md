# The vector analysis cache

The program keeps the vector scene of every picture (spec section 10.2) in
files, so that entering a room loads a scene instead of analysing the
picture again. The scene format is in `src/sci-vector`
(`sci/vector/scene_io.hpp`, bytes in and bytes out); the cache and its files
are in `sci-platform` (`sci/platform/analysis_cache.hpp`).

## Location and file names

The cache is the directory `cache/` next to `sierra-hd.ini`. Each entry is
one file named after its variant:

```
pic.<number, 3 digits>.<visual|priority>[.mirrored][.palette<n>]{.over.<number, 3 digits>[.mirrored][.palette<n>]}.scene
```

For example `pic.044.visual.scene`, or
`pic.005.priority.mirrored.palette2.scene`. A picture stack, a picture with
overlays drawn over it (kDrawPic with add-to, spec section 6.8), is a
variant of its own: each overlay adds `.over.` with its number and options,
in drawing order, for example `pic.174.visual.over.074.scene`. Its scene is
built from one trace of the base picture and the overlays drawn over it,
so the overlays are layers of the scene in drawing order. An entry is written to
`<name>.tmp` and then renamed, so a write that is cut off leaves no entry.
Deleting the directory is always safe; it is rebuilt.
For the tested release, the 176 entries of the first launch take 83 MB
(0.06 to 1.5 MB each).

## When entries are made

- On a launch, the program checks every picture's two planes, as drawn
  without options (not mirrored, palette 0). It analyses the ones without a
  valid entry on the executor in batches of 16 and shows a progress bar.
  Closing the window cancels the analysis and quits; the entries written so
  far are kept.
- Any other variant (mirrored, another palette, a picture stack with
  overlays), or an entry that is still missing, is analysed the first time
  it is asked for, and then stored.
- The last 8 scenes asked for are also kept in memory.

## Key and invalidation

Every entry stores a 32-byte key, the SHA-256 of the following:

| Field | Encoding |
|---|---|
| game digest | 32 bytes: SHA-256 over every resource in the manager's order (u32 type, u32 number, u32 size, data; an unreadable resource is u32 type, u32 number, u32 0xFFFFFFFF) |
| analyser version | u32, `sci::vector::analyser_version` |
| scene format version | u32, `sci::vector::scene_format_version` |
| hint file | u32 size, then the bytes of the picture's hint file (size 0 when there is none) |
| picture | u32 |
| plane | u32: 0 visual, 1 priority |
| mirrored | u32: 0 or 1 |
| EGA palette | u32 |
| per overlay, in drawing order | u32 picture, u32 mirrored (0 or 1), u32 EGA palette, u32 size and the bytes of the overlay's hint file (size 0 when there is none) |

Without overlays, nothing follows the EGA palette: the keys of the
variants of the first cache format did not change when overlays were
added (milestone 5), so their entries stay valid and neither the entry
nor the scene format version changed.

An entry is stale when its key differs from the one computed now. It is
also refused when it has another entry version or cannot be read. Such an
entry is analysed again and replaced, and this is logged at the info level.

`analyser_version` must be increased with every change that changes a
scene, which means every change of `tests/golden/pictures.txt`.

All integers are little-endian.

## Entry file

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `SHDC` |
| 4 | 2 | entry version, 1 |
| 6 | 32 | key |
| 38 | rest | the scene |

## Scene

A double is stored as its IEEE 754 binary64 bits (u64), so reading a scene
back gives the same values bit for bit. A point is two doubles (x, y).

| Field | Encoding |
|---|---|
| magic | `SHDS` |
| format version | u16, 1 |
| plane | u8: 0 visual, 1 priority |
| faces | u32 count; per face u8 fill (0 uniform, 1 blocks), u8 value |
| scan edges | u32 count; per edge top point, bottom point, i32 face after |
| block values | 60800 bytes (320 x 190), row by row |
| layers | u32 count; per layer a u8 tag, then its fields |

| Tag | Layer | Fields |
|---|---|---|
| 1 | Stroke | i32 source, u32 count and the points, f64 radius, u8 value, u8 fitted (0 or 1), u32 count and the cuts (point, normal) |
| 2 | Ellipse | i32 source, centre point, f64 radius x, f64 radius y, u8 value |
| 3 | Box | i32 source, f64 left, top, right, bottom, u8 value |
| 4 | Blocks | i32 source, u32 count, then per pixel u32 pixel index, u8 value |

The reader refuses a scene in these cases:
- a count larger than the remaining bytes can hold;
- a double that is not finite;
- an unknown layer tag, or a face fill or flag outside its values;
- a stroke without points;
- an edge whose top is not above its bottom, or whose face is not in the
  list;
- block pixels that do not ascend or that lie outside the picture;
- bytes left after the last layer.

`tests/fuzz/fuzz_scene.cpp` checks that every scene the reader accepts
writes back to the same bytes.
