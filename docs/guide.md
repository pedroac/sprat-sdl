# sprat-sdl guide

sprat-sdl loads sprite sheets made by [sprat-cli](https://github.com/pedroac/sprat-cli)
and draws them with SDL3. It reads the sheet with
[sprat-lib](https://github.com/pedroac/sprat-lib), loads the atlas images, and
gives you frames, sprites, animations, markers and puppets. See
[`examples/sprite_sheet.c`](../examples/sprite_sheet.c) for a complete example.

## Quick start

```c
SPRAT_Sheet *sheet = SPRAT_LoadSheet(renderer, "hero.sprat");

SPRAT_Sprite hero = {
    sheet, 0,           /* sheet, frame */
    400.0f, 300.0f,     /* x, y */
    2.0f, 2.0f,         /* scale_x, scale_y */
    0.0, SDL_FLIP_NONE  /* angle, flip */
};
SPRAT_RenderSprite(renderer, &hero);

SPRAT_DestroySheet(sheet);
```

`SPRAT_Sprite` is a plain struct. Set its fields and draw it. Set both scales:
a scale of `0.0f` makes the sprite invisible.

## Concepts

| Term | Meaning |
|---|---|
| **Sheet** | A `.sprat` file plus its atlas images. |
| **Frame** | One image in the sheet. |
| **Sprite** | A frame placed on screen: position, scale, flip and angle. |
| **Animation** | A list of frames played at an FPS. |
| **Puppet** | A paper-cutout character: parts joined by markers, posed and animated. |
| **Marker** | A named point, circle, rectangle or polygon on a frame. |
| **Pivot** | The point a frame scales and rotates around. |
| **Nine-slice** | A frame stretched without squashing its corners. |

## Reference

Every function links to its full page. The one-line description is enough to
pick the right one. Run `doxygen docs/Doxyfile` to build the pages under
`docs/api/`.

### Library

| Function | Description |
|---|---|
| [`SPRAT_Version`](api/SDL__sprat_8h.html#SPRAT_Version) | The library version. |

### Sheets

| Function | Description |
|---|---|
| [`SPRAT_LoadSheet`](api/SDL__sprat_8h.html#SPRAT_LoadSheet) | Load a sheet and its atlas textures. |
| [`SPRAT_LoadSheet_IO`](api/SDL__sprat_8h.html#SPRAT_LoadSheet_IO) | Load a sheet from an `SDL_IOStream`. |
| [`SPRAT_DestroySheet`](api/SDL__sprat_8h.html#SPRAT_DestroySheet) | Free a sheet and its textures. |

### Frames

| Function | Description |
|---|---|
| [`SPRAT_GetFrameFromName`](api/SDL__sprat_8h.html#SPRAT_GetFrameFromName) | Find a frame by path or unique name. |
| [`SPRAT_GetFrameMarker`](api/SDL__sprat_8h.html#SPRAT_GetFrameMarker) | Get a marker of a frame. |
| [`SPRAT_RenderFrame`](api/SDL__sprat_8h.html#SPRAT_RenderFrame) | Draw a frame with its pivot at a point. |
| [`SPRAT_RenderFrame9Grid`](api/SDL__sprat_8h.html#SPRAT_RenderFrame9Grid) | Stretch a frame as a nine-slice. |

### Sprites

| Function | Description |
|---|---|
| [`SPRAT_RenderSprite`](api/SDL__sprat_8h.html#SPRAT_RenderSprite) | Draw a sprite. |
| [`SPRAT_GetSpriteMarker`](api/SDL__sprat_8h.html#SPRAT_GetSpriteMarker) | Get a marker of a sprite's frame. |
| [`SPRAT_GetSpritePoint`](api/SDL__sprat_8h.html#SPRAT_GetSpritePoint) | Where a point of the image lands on screen. |

### Markers

| Function | Description |
|---|---|
| [`SPRAT_GetMarkerCenter`](api/SDL__sprat_8h.html#SPRAT_GetMarkerCenter) | Where a marker is: its point or center. |
| [`SPRAT_GetMarkerOutline`](api/SDL__sprat_8h.html#SPRAT_GetMarkerOutline) | Points to draw a marker's outline. |
| [`SPRAT_MarkersOverlap`](api/SDL__sprat_8h.html#SPRAT_MarkersOverlap) | Do two markers overlap? |

### Animations

| Function | Description |
|---|---|
| [`SPRAT_GetAnimationFromName`](api/SDL__sprat_8h.html#SPRAT_GetAnimationFromName) | Find an animation by name. |
| [`SPRAT_InitAnimationPlayer`](api/SDL__sprat_8h.html#SPRAT_InitAnimationPlayer) | Start a player at the first frame. |
| [`SPRAT_SetAnimation`](api/SDL__sprat_8h.html#SPRAT_SetAnimation) | Switch animation, restarting only on a change. |
| [`SPRAT_UpdateAnimation`](api/SDL__sprat_8h.html#SPRAT_UpdateAnimation) | Advance the player by seconds. |
| [`SPRAT_PlayAnimation`](api/SDL__sprat_8h.html#SPRAT_PlayAnimation) | Play from the start (or end, in reverse). |
| [`SPRAT_StopAnimation`](api/SDL__sprat_8h.html#SPRAT_StopAnimation) | Stop and rewind. |
| [`SPRAT_PauseAnimation`](api/SDL__sprat_8h.html#SPRAT_PauseAnimation) | Pause, keeping the time. |
| [`SPRAT_ResumeAnimation`](api/SDL__sprat_8h.html#SPRAT_ResumeAnimation) | Resume a paused player. |
| [`SPRAT_IsAnimationFinished`](api/SDL__sprat_8h.html#SPRAT_IsAnimationFinished) | Did a `once` animation finish? |
| [`SPRAT_GetAnimationFrame`](api/SDL__sprat_8h.html#SPRAT_GetAnimationFrame) | The frame shown now. |
| [`SPRAT_SetSpriteAnimationFrame`](api/SDL__sprat_8h.html#SPRAT_SetSpriteAnimationFrame) | Set a sprite's frame from a player. |
| [`SPRAT_UpdateSpriteAnimation`](api/SDL__sprat_8h.html#SPRAT_UpdateSpriteAnimation) | Advance a player and set a sprite. |
| [`SPRAT_RenderAnimation`](api/SDL__sprat_8h.html#SPRAT_RenderAnimation) | Draw the current frame. |
| [`SPRAT_RenderAnimationRotated`](api/SDL__sprat_8h.html#SPRAT_RenderAnimationRotated) | Draw it scaled, flipped and rotated. |

### Puppets

| Function | Description |
|---|---|
| [`SPRAT_CreatePuppet`](api/SDL__sprat_8h.html#SPRAT_CreatePuppet) | Create a puppet in its rest pose. |
| [`SPRAT_DestroyPuppet`](api/SDL__sprat_8h.html#SPRAT_DestroyPuppet) | Free a puppet. |
| [`SPRAT_GetPuppetPartFromName`](api/SDL__sprat_8h.html#SPRAT_GetPuppetPartFromName) | Find a part by name. |
| [`SPRAT_GetPuppetPoseFromName`](api/SDL__sprat_8h.html#SPRAT_GetPuppetPoseFromName) | Find a pose by name. |
| [`SPRAT_GetPuppetSequenceFromName`](api/SDL__sprat_8h.html#SPRAT_GetPuppetSequenceFromName) | Find a sequence by name. |
| [`SPRAT_ResetPuppetPose`](api/SDL__sprat_8h.html#SPRAT_ResetPuppetPose) | Go back to the rest pose. |
| [`SPRAT_ApplyPuppetPose`](api/SDL__sprat_8h.html#SPRAT_ApplyPuppetPose) | Apply a pose over the current one. |
| [`SPRAT_ApplyPuppetSequence`](api/SDL__sprat_8h.html#SPRAT_ApplyPuppetSequence) | Apply a sequence at a time. |
| [`SPRAT_GetPuppetSequenceDuration`](api/SDL__sprat_8h.html#SPRAT_GetPuppetSequenceDuration) | How long one cycle takes. |
| [`SPRAT_IsPuppetSequenceFinished`](api/SDL__sprat_8h.html#SPRAT_IsPuppetSequenceFinished) | Did a `once` sequence finish? |
| [`SPRAT_UpdatePuppet`](api/SDL__sprat_8h.html#SPRAT_UpdatePuppet) | Advance the parts' animations. |
| [`SPRAT_GetPuppetPartSprite`](api/SDL__sprat_8h.html#SPRAT_GetPuppetPartSprite) | A part as a `SPRAT_Sprite`. |
| [`SPRAT_GetPuppetPartColor`](api/SDL__sprat_8h.html#SPRAT_GetPuppetPartColor) | A part's tint and opacity. |
| [`SPRAT_RenderPuppet`](api/SDL__sprat_8h.html#SPRAT_RenderPuppet) | Draw all parts in order. |

## How to

### Draw a sprite

```c
SPRAT_Sprite hero = {
    sheet, frame,       /* sheet, frame */
    x, y,               /* x, y */
    2.0f, 2.0f,         /* scale_x, scale_y */
    0.0, SDL_FLIP_NONE  /* angle, flip */
};

hero.x += speed * dt;
hero.angle = 15.0;
hero.scale_x = facing_left ? -2.0f : 2.0f;
SPRAT_RenderSprite(renderer, &hero);
```

The frame is scaled, mirrored around its pivot, then rotated clockwise around
it. A negative scale mirrors. `scale_x` and `scale_y` are not `sheet->scale`:
use `1.0f / sheet->scale` to draw at the art's original size.

### Play an animation

```c
const SPRAT_Animation *run_anim = SPRAT_GetAnimationFromName(sheet, "run");
SPRAT_AnimationPlayer run;

SPRAT_InitAnimationPlayer(&run, run_anim);
SPRAT_UpdateAnimation(&run, dt);
SPRAT_RenderAnimation(renderer, sheet, &run, x, y);
```

To keep a sprite's markers in step, use `SPRAT_UpdateSpriteAnimation(&hero, &run, dt)`.
To share one player between sprites, use `SPRAT_SetSpriteAnimationFrame()`.

`SPRAT_SetAnimation()` switches animations and restarts only when the animation
changes:

```c
SPRAT_SetAnimation(&run, moving ? run_anim : idle_anim);
```

| Loop mode | Frames 0 1 2 play as |
|---|---|
| `SPRAT_LOOPMODE_REPEAT` | 0 1 2, 0 1 2, ... |
| `SPRAT_LOOPMODE_ONCE` | 0 1 2, stop on 2 |
| `SPRAT_LOOPMODE_PINGPONG` | 0 1 2 1, ... |
| `SPRAT_LOOPMODE_ONCE_PINGPONG` | 0 1 2 1 0, stop on 0 |

### Use markers

```c
const SPRAT_Marker *hand = SPRAT_GetSpriteMarker(&hero, "hand");
SDL_FPoint grip;

if (hand && SPRAT_GetMarkerCenter(&hero, hand, &grip)) {
    sword.x = grip.x;
    sword.y = grip.y;
}
```

Markers are in the frame's original-image pixels. The marker functions place
them the same way `SPRAT_RenderSprite()` places the frame, so what you draw and
what you collide always agree.

```c
const SPRAT_Marker *hitbox = SPRAT_GetSpriteMarker(&hero, "hitbox");
SDL_FPoint outline[257];
int count = SPRAT_GetMarkerOutline(&hero, hitbox, outline, 257);

if (count > 0) {
    SDL_RenderLines(renderer, outline, SDL_min(count, 257));
}
```

```c
/* coin is another sprite; coin_hitbox is a marker of its frame */
if (SPRAT_MarkersOverlap(&hero, hitbox, &coin, coin_hitbox)) {
    /* picked */
}
```

### Stretch a nine-slice

```c
SDL_FRect panel = { 20.0f, 20.0f, 300.0f, 120.0f };
SPRAT_RenderFrame9Grid(renderer, sheet, panel_frame, 2.0f, &panel);
```

`dstrect` covers the whole original image. `scale` scales the corners; `0.0f`
keeps them at their size. The middle stretches. A frame without a slice is
simply stretched.

### Pose a puppet

```c
SPRAT_Puppet *kid = SPRAT_CreatePuppet(sheet, "kid");
int walk = SPRAT_GetPuppetSequenceFromName(kid, "walk");

SPRAT_UpdatePuppet(kid, dt);
kid->x = x;
kid->y = ground_y;
kid->flip = facing_left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

SPRAT_ResetPuppetPose(kid);
SPRAT_ApplyPuppetSequence(kid, walk, walk_time);
SPRAT_RenderPuppet(renderer, kid);

SPRAT_DestroyPuppet(kid);
```

Reset the pose every update, then apply the sequences and poses you want. Later
poses win where they set the same part. `SPRAT_GetPuppetPartSprite()` gives a
part as a sprite, so its markers and collisions work.

## Building

sprat-sdl needs SDL 3.4+ and sprat-lib 0.15+.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library. |
| `SPRAT_SDL_BUILD_EXAMPLES` | `ON` when top level | Build `sprite_sheet`. |
| `SPRAT_SDL_SANITIZE` | `OFF` | Build with ASan and UBSan. |
| `SPRAT_SDL_INSTALL` | `ON` with installed SDL3 and sprat | Generate install rules. |

To build against a local sprat-lib:

```sh
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_SPRAT=../sprat-lib
```

To build the API pages:

```sh
doxygen docs/Doxyfile
```

They go to `docs/api/`.

## Errors

Functions that can fail return `NULL`, `-1` or `false` and set `SDL_GetError()`:

```c
SPRAT_Sheet *sheet = SPRAT_LoadSheet(renderer, "hero.sprat");
if (!sheet) {
    SDL_Log("Couldn't load the sheet: %s", SDL_GetError());
}
```

Nothing is logged. Names from the sheet are escaped before they go into an
error message.

## Loading

`SPRAT_LoadSheet()` loads the sheet and its atlas images. Pass `NULL` as the
renderer to load only the metadata, then load the textures yourself:

```c
SPRAT_Sheet *sheet = SPRAT_LoadSheet(NULL, "hero.sprat");
for (int i = 0; i < sheet->num_atlases; i++) {
    sheet->atlases[i].texture =
        IMG_LoadTexture(renderer, sheet->atlases[i].path);
}
```

Atlases are loaded with `SDL_LoadSurface()`, which reads BMP and PNG. The sheet
fails to load if an atlas is missing or the wrong size.

Atlas paths are used as written. A sheet from an untrusted source could name
any file: check `sheet->atlases[i].path` with sprat-lib's `sprat_path_is_safe()`
first.

## Memory

A sheet is one allocation. Destroy it with `SPRAT_DestroySheet()` before the
renderer. Each `SPRAT_CreatePuppet()` is one more allocation, freed with
`SPRAT_DestroyPuppet()`. Destroy puppets before their sheet.

## Limitations

- Atlas paths are used as written; check them for untrusted sheets.
- Nine-slices always render as `SPRAT_SLICEMODE_STRETCH`. The slice mode tells
  you what the artist chose.
