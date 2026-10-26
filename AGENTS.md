# AGENTS.md

Guidance for AI agents (and humans) working on **sprat-sdl**.

## Purpose

sprat-sdl loads sprite sheets made by [sprat-cli](https://github.com/pedroac/sprat-cli)
into SDL3 and renders them. Parsing and validation belong to
[sprat-lib](https://github.com/pedroac/sprat-lib); this library converts its model into
SDL types, loads the atlas textures, and does the rendering math. Its sibling for
raylib is [sprat-raylib](https://github.com/pedroac/sprat-raylib): keep the behavior of
the two the same, and the API of each true to its engine.

## Priorities

1. **Follow SDL3's design.** A compiled library like SDL_image and SDL_ttf:
   `#include <SDL3_sprat/SDL_sprat.h>`, `SPRAT_` + `PascalCase` functions
   (`Load*`/`Destroy*`/`Get*From*`/`Render*`, `_IO` variants taking an `SDL_IOStream`
   and `closeio`), `SDL_DECLSPEC`/`SDLCALL`, `snake_case` fields and parameters, `num_*`
   counts, and enum values named `SPRAT_TYPENAME_VALUE`. SDL types in the API
   (`SDL_FRect`, `SDL_FPoint`, `SDL_FlipMode`, `double` angles). Functions that can fail
   return `NULL`, `-1` or `false` and set `SDL_SetError()`; nothing is logged. Only SDL
   is a public dependency: `sprat/sprat.h` is included by `src/SDL_sprat.c` alone.
   Every public declaration has an SDL-style Doxygen comment (`\param`, `\returns`,
   `\threadsafety`, `\since`, `\sa`). SDL's code style: 4 spaces, braces on the
   function's next line and on the statement's line otherwise, braces around every
   body, `/* */` comments, SDL_stdinc functions (`SDL_strcmp`, `SDL_calloc`...) rather
   than the C library's.
2. **Render exactly what was authored.** Frames are placed by their pivot with trims
   and packing rotation undone, each in one `SDL_RenderTexture()` or
   `SDL_RenderTextureRotated()` call with `SDL_FRect` rectangles (the software
   renderer gets the same copy as `SDL_RenderTextureAffine()` corners, since it rounds
   rotated rectangles). Every change to the rendering math needs the pixel tests in
   `tests/` to pass, on a hardware renderer and on `SDL_RENDER_DRIVER=software`.
3. **Keep the sheet's order.** `frames[i]` is the sheet's `sprites[i]` and
   `animations[i]` its `animations[i]`, so sprat-lib's binding indexes work as they are.

## Code layout

- `include/SDL3_sprat/SDL_sprat.h`: the public API. It needs only SDL3.
- `src/SDL_sprat.c`: the implementation.
- `tests/test_sprat_sdl.c`: runs without a window. It includes `SDL_sprat.c`, so it
  can call internal functions (`AddSize()`, `JoinPath()`, `Get9GridDrawCalls()`...).
  Renders go to a target texture of a hardware renderer, on an offscreen window, and
  are compared pixel by pixel with the original image. Without a hardware renderer it
  falls back to the software renderer, which only draws whole-pixel geometry exactly,
  and skips the rest (`SPRAT_TEST_REQUIRE_HARDWARE=1` makes that a failure).
  `tests/lsan.supp` silences the GPU driver's and D-Bus's leaks at exit.
- `examples/sprite_sheet.c`: the demo, written with SDL's main callbacks.
  `examples/resources/` holds a sheet packed by sprat-cli (`make_resources.sh`) from
  frames drawn by `make_sources.py`. `examples/sprite_sheet.png` is its screenshot.

## Coding practices

- C99, warning-free with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, and the
  header usable from C++.
- Pass names and paths from a sheet through sprat-lib's `sprat_escape()` (`Escape()`)
  before they go into an error message.
- Allocate with `SDL_calloc`/`SDL_free`. A sheet is one allocation that starts with
  the `SPRAT_Sheet`; don't add per-record allocations.
- Check every size computed from the input for overflow (`AddSize()`).

## Build and test

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DSPRAT_SDL_SANITIZE=ON \
  -DFETCHCONTENT_SOURCE_DIR_SPRAT=../sprat-lib
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Add or update tests for every behavior change or bug fix. When the rendering changes,
also run `build/bin/sprite_sheet` and look at it.

## Versioning and commits

- Update `VERSION` (`vMAJOR.MINOR.PATCH`) and the `SDL_SPRAT_*_VERSION` macros in the
  header in the same commit as the change. New API gets a
  `\since This function is available since sprat-sdl X.Y.Z.` line.
- Commit messages: `feat: ...`, `fix: ...`, `docs: ...`, `test: ...`, `refactor: ...`.
- Never commit build output.
