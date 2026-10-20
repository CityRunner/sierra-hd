# SCI0 resource files

The format of the game's resource files as the program reads them
(`src/sci-resource`). Everything here was ported from ScummVM's resource
loader at the pinned revision (`engines/sci/resource/`). The
behaviour is checked against the reference ScummVM for every resource of the
tested release (`tools/resource-compare`). All numbers are little-endian.

## Files

| File | Content |
|---|---|
| `RESOURCE.MAP` | Index of all resources |
| `RESOURCE.0nn` | Volumes holding the resource data, numbered by the extension (`resource.0` followed by two digits) |
| Patch files, e.g. `SCRIPT.052` | Single resources that replace or add one resource |

File names are matched without regard to case. Only files directly in the game
folder are read.

## Resource ids and types

A resource id is a type and a number. In the map and in volume headers it is
one 16-bit word: type in bits 11-15, number in bits 0-10. The type numbers of
SCI0 are:

| Number | Type | Number | Type |
|---|---|---|---|
| 0 | view | 8 | cursor |
| 1 | pic | 9 | patch |
| 2 | script | 10 | bitmap |
| 3 | text | 11 | palette |
| 4 | sound | 12 | cdaudio |
| 5 | memory | 13 | audio |
| 6 | vocab | 14-20 | sync, message, map, heap, audio36, sync36, xlate |
| 7 | font | 21 | rave; anything higher is invalid |

The top bit of a type byte is ignored. The tested release uses view, pic,
script, text, sound, vocab, font, cursor and patch.

## Resource map

A sequence of 6-byte entries: the id word, then a 32-bit location (volume
number in bits 26-31, byte offset in the volume in bits 0-25). An entry with
location `0xFFFFFFFF` ends the map.

- When an id appears more than once, the first entry counts.
- An entry whose offset lies at or beyond the end of its volume is skipped,
  with a warning.
- A map that ends without the terminating entry is rejected. ScummVM keeps
  the entries read so far and warns.

Format checks before reading (ScummVM's `detectMapVersion`):

1. The last four bytes must be `FF FF FF FF`. Otherwise the map is from SCI1
   or later.
2. If the last seven bytes start with `FF FF FF`, the map is the KQ5 FM-Towns
   variant.
3. Every entry up to the first one starting with `FF FF FF` must name an
   existing volume. Otherwise the map is the SCI01 layout, which uses fewer
   bits for the volume.

Maps that fail these checks are reported as unsupported.

## Volumes

Each resource in a volume starts with an 8-byte header:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Id word. When this differs from the map's id, ScummVM uses it for the loaded resource; the program reports it as `loaded_id`. |
| 2 | 2 | Packed size + 4 |
| 4 | 2 | Unpacked size |
| 6 | 2 | Compression method |

The packed data follows the header. The first volume in name order must pass
ScummVM's SCI0 plausibility check (`detectVolVersion`) over its first
megabyte:
- the method is at most 4;
- a stored entry's packed size is its unpacked size + 4;
- the unpacked size is at least the packed size.

## Compression

| Method | SCI0 meaning | Tested release |
|---|---|---|
| 0 | Stored: the packed bytes are the data | 46 entries |
| 1 | LZW, codes least significant bit first | 1270 entries |
| 2 | Huffman, bits most significant first | 96 entries |
| 3, 4, 18-20 | Methods of later SCI versions | rejected as unsupported |

Both decoders read from the volume file, not from a buffer of the packed
size. They fetch whole bytes ahead of need, and bytes beyond the end of the
volume read as zero. Decoding stops when the full unpacked size has been
written and at least the packed size has been fetched, or at an end marker.

### LZW (method 1)

- Codes start at 9 bits.
- Code 256 resets the table, code 257 ends the data, and codes 0-255 are
  literal bytes.
- After every code, the table gains an entry: the string just output plus the
  first byte of the next one. The decoder records it as offset and length into
  the output (16-bit each).
- Codes widen by one bit when the table reaches 512, 1024 and 2048 entries.
  The table stops growing at 4096 entries.
- A code beyond the table stops decoding.
- Decoding succeeds if the end marker was reached or the full size was
  written. After an early end marker, the rest of the output is filled with
  zeros; ScummVM leaves that memory uninitialised.

### Huffman (method 2)

The data begins with:
- a node count n;
- a terminator byte t;
- n two-byte nodes.

A node whose second byte is zero is a leaf, and its first byte is the output.
Otherwise the second byte holds two forward links, counted in nodes: the high
nibble for bit 0 and the low nibble for bit 1. A low nibble of zero on bit 1
means that the next 8 bits are a literal byte. A literal equal to t ends the
data.

The program rejects a zero high nibble (a link to the node itself, where
ScummVM loops forever) and links leaving the table.

## Patch files

Patch files are found by name, in ScummVM's order:
- types in the order of the table above (rave excluded);
- within a type, first names of the SCI0 form `<type>.nnn` (three digits);
- then names of the SCI1 form `nnn.<suffix>`. The suffixes are `v56`, `p56`,
  `scr`, `tex`, `snd`, `voc`, `fon`, `cur`, `pat`, `bit`, `pal`, `cda`, `aud`,
  `syn`, `msg`, `map`, `hep` and `trn`, plus `v16/v32/v64`, `p16/p32/p64` and
  `csc`.

Within each group, files are taken in name order (ScummVM's order there
depends on the directory listing). A later patch for the same resource
replaces an earlier one, and a patch may add a resource the map does not
have.

ScummVM's quirks are kept:
- the number of an SCI1-form name is read with `strtol` up to a required `.`;
- for other names, the character after the separator must not be a letter,
  and the number is read with `atoi`.

A patch file starts with the type byte (bit 7 set) and the size h of an extra
header. The data starts at offset 2 + h.

The file is ignored if:
- it is shorter than 3 bytes;
- its type is not the expected one;
- 2 + h reaches the end of the file.

A file starting with `RIFF` counts as an audio patch.

The tested release has two patch files, `SCRIPT.052` and `TEXT.052`.
