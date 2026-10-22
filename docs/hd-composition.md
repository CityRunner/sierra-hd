# HD composition of the game screen (milestone 5)

How the program builds the HD frame from the interpreter's state (spec
sections 6.6, 6.8 and 6.9). The interpreter's 1x maps stay the truth for
the game logic and for original mode; HD mode only changes how the frame
is drawn.

## What the interpreter keeps

Beside the visual, priority and control maps, sci-vm keeps:

- **The owner map** (one byte per 1x pixel, replacing the 4b cover map):
  what last drew the visual pixel.
  - `Picture`: a picture or a picture overlay (`kDrawPic`, with or without
    add-to).
  - `AddToPic`: a static view drawn into the picture (`kAddToPic`).
  - `Sprite`: a cel drawn by `kAnimate`.
  - `Primitive`: a kernel drawing primitive into the visual map (`kGraph`
    line or box), outside a window.
  - `Interface`: everything else: text, windows, menus, controls,
    `kDrawCel`, the menu bar, kernel primitives inside a window.

  The drawing code sets the current owner for its scope; `Screen::put_pixel`
  writes it with the visual pixel. Saved and restored bits carry the owner
  map, in place of ScummVM's display-map copy (as the cover map did).
- **The priority source map** (one bit per 1x pixel): whether the priority
  value came from a picture (`Picture`) or from later drawing (`AddToPic`,
  `Primitive`).
- **The picture stack** of the shown picture: the base picture and the
  overlays drawn on it, in order, each with its options (mirrored, EGA
  palette). `kDrawPic` without add-to starts a new stack.
- **The primitives** drawn into the visual map outside windows since the
  picture was drawn: lines (end points, colour) and boxes (rectangle,
  colour), in order.
- **The background layer:** the visual map, owner map and priority maps as
  they were when `kAnimate` began drawing cels (`draw_cels`), that is
  everything under the sprites.
- **The sprite list:** the cels `kAnimate` drew, in drawing order: view,
  loop, cel, mirrored, the cel rectangle on screen, priority, and the clip
  rectangle (the picture port).

The background layer and the sprite list are pending until the screen
shows them (`update_screen` and `bitsShow` of the animate cycle). Then they
become the shown ones, together with the shown screen. `kDrawPic` clears
the sprite list.

## How the program draws a frame in HD mode

For each 1x pixel p of the shown screen, at scale N:

1. **Interface on top.** If the shown owner of p is `Interface`, the N x N
   block takes p's EGA colour. Nothing else draws there.
2. **The background.** Otherwise the block starts from the background layer:
   - `Picture`: the HD background, rendered from the vector scene of the
     picture stack (base and overlays in order, spec section 6.8), with the
     primitives' lines as vector strokes over it;
   - `AddToPic`, `Primitive` (boxes), `Interface` and anything else: the
     N x N block of the background layer's EGA colour (an addToPic cel
     scaled N x N is exactly that).
3. **The sprites,** in drawing order. For each opaque pixel q of a sprite's
   cel, clipped to its clip rectangle and to pixels whose shown owner is
   not `Interface`, each HD sub-pixel h of q takes the cel's colour if the
   sprite's priority is at least the HD priority at h.
   - The HD priority at h comes from the vector priority scene, rendered at
     N (smooth occlusion edges), where the priority source of q is a
     picture.
   - Elsewhere (addToPic, primitives) it is q's 1x priority as an N x N
     block.
   - With `priority_snap = on` in the configuration, the HD priority is
     always the 1x priority as N x N blocks: occlusion then follows the
     sprite grid exactly as in the original.

**Where HD differs from the original.** A sprite's own pixels are always
original pixels scaled N x N, since that equals the EGA block. HD changes
only two things:
- the background under and around the sprites;
- the occlusion edge where a sprite passes behind a foreground object.

There, the vector priority decides each HD sub-pixel, while the 1x
priority decides whole blocks.

Original mode draws every pixel as the N x N block of its shown EGA colour,
unchanged.

## Caching

- The HD background and the HD priority map are rendered once per picture
  stack, list of primitive lines and N, and are kept while that state
  stays the same.
- The scenes come from the analysis cache (spec section 10.2). A picture
  stack with overlays is a cache variant of its own, analysed and stored
  the first time it is needed.
- Per frame, only the changed rectangles are recomposed: the sprites' old
  and new rectangles, and what the shown screen changed.
