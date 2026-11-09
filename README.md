# sprat-sdl

Load and draw [sprat-cli](https://github.com/pedroac/sprat-cli) sprite sheets with
[SDL3](https://libsdl.org/).

sprat-sdl is a small C99 library in the style of SDL_image and SDL_ttf. It reads
the sheet with [sprat-lib](https://github.com/pedroac/sprat-lib), loads the atlas
images, and draws frames, sprites, animations, markers and puppets.

![Example](examples/sprite_sheet.png)

```c
#include <SDL3_sprat/SDL_sprat.h>

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

## Documentation

| Page | What is there |
|---|---|
| [Guide](docs/guide.md) | Quick start, concepts, how-tos, and tables of every function. |
| [API reference](https://github.com/pedroac/sprat-sdl/blob/main/include/SDL3_sprat/SDL_sprat.h) | Every function, type and field, in the header. |
| [Example](examples/sprite_sheet.c) | Animated sprites, markers, collisions and nine-slices. |

Run `doxygen docs/Doxyfile` to build the API pages under `docs/api/`.

## Building

sprat-sdl needs SDL 3.4+ and sprat-lib 0.15+.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

With CMake, `sprat-sdl::sprat-sdl` brings SDL3 along:

```cmake
add_subdirectory(sprat-sdl)
target_link_libraries(game PRIVATE sprat-sdl::sprat-sdl)
```

To build against a local sprat-lib, pass
`-DFETCHCONTENT_SOURCE_DIR_SPRAT=../sprat-lib`.

## Example

[`examples/sprite_sheet.c`](examples/sprite_sheet.c) renders every frame of the
example sheet, plays the `run`, `run-left`, `spin` and `spin-pingpong`
animations, resizes a nine-slice panel, and lets you run a hero holding a sword
at its `hand` marker. The sheet in `examples/resources/` is packed by
`make_resources.sh` from frames drawn by `make_sources.py`.

## License

MIT. See [LICENSE](LICENSE).
