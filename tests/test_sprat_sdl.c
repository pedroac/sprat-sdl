/*
  Tests sprat-sdl without a visible window: loading sheets, the queries, the
  animation player, and every kind of rendering. Renders go to a render target,
  on a hardware renderer when there is one, and every pixel is compared with the
  original image placed at its pivot.

  The library's source is compiled in, to reach its internal functions.
*/
#include "SDL_sprat.c"

#include <math.h>      /* NAN, INFINITY */
#include <stdio.h>

static int failures = 0;

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

#define CHECK(cond) \
    do { if (!(cond)) { fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define CHECK_INT(actual, expected) \
    do { long long a_ = (long long)(actual), e_ = (long long)(expected); \
         if (a_ != e_) { fprintf(stderr, "%s:%d: %s is %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_); failures++; } } while (0)
#define CHECK_FLOAT(actual, expected) \
    do { double a_ = (double)(actual), e_ = (double)(expected); \
         if (SDL_fabs(a_ - e_) > 1e-4) { fprintf(stderr, "%s:%d: %s is %g, expected %g\n", __FILE__, __LINE__, #actual, a_, e_); failures++; } } while (0)
#define CHECK_STR(actual, expected) \
    do { const char *a_ = (actual), *e_ = (expected); \
         if (a_ == NULL || SDL_strcmp(a_, e_) != 0) { fprintf(stderr, "%s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #actual, a_ ? a_ : "(null)", e_); failures++; } } while (0)
#define CHECK_ERROR(substring) \
    do { const char *e_ = SDL_GetError(); \
         if (SDL_strstr(e_, (substring)) == NULL) { fprintf(stderr, "%s:%d: error is \"%s\", expected it to contain \"%s\"\n", __FILE__, __LINE__, e_, (substring)); failures++; } } while (0)

/* ---- Memory --------------------------------------------------------------- */

/* The size of the last calloc, to check what a sheet costs */
static size_t last_calloc_size = 0;
static SDL_malloc_func original_malloc;
static SDL_calloc_func original_calloc;
static SDL_realloc_func original_realloc;
static SDL_free_func original_free;

/* Set to make SDL_calloc() fail, as when memory runs out */
static bool fail_callocs = false;

/* SDL's allocations not yet freed, counted from when the functions below are set. SDL's own
   count, SDL_GetNumAllocations(), is only kept by builds that enable it */
static int live_allocations = 0;

static void * SDLCALL counting_malloc(size_t size)
{
    void *memory = original_malloc(size);

    if (memory) {
        live_allocations++;
    }
    return memory;
}

static void * SDLCALL recording_calloc(size_t count, size_t size)
{
    void *memory;

    if (fail_callocs) {
        return NULL;
    }
    last_calloc_size = count * size;
    memory = original_calloc(count, size);
    if (memory) {
        live_allocations++;
    }
    return memory;
}

/* SDL_realloc() never frees: it asks for at least one byte */
static void * SDLCALL counting_realloc(void *memory, size_t size)
{
    void *result = original_realloc(memory, size);

    if (result && !memory) {
        live_allocations++;
    }
    return result;
}

static void SDLCALL counting_free(void *memory)
{
    if (memory) {
        live_allocations--;
    }
    original_free(memory);
}

/* An allocator for sprat-lib that has no memory to give */
static void *failing_malloc(size_t size)
{
    (void)size;
    return NULL;
}

static void *failing_calloc(size_t count, size_t size)
{
    (void)count;
    (void)size;
    return NULL;
}

static void hook_memory_functions(void)
{
    SDL_GetOriginalMemoryFunctions(&original_malloc, &original_calloc, &original_realloc, &original_free);
    SDL_SetMemoryFunctions(counting_malloc, recording_calloc, counting_realloc, counting_free);
}

/* ---- Loading -------------------------------------------------------------- */

static SPRAT_Sheet *load_text(const char *text)
{
    SPRAT_Sheet *sheet = SPRAT_LoadSheet_IO(NULL, SDL_IOFromConstMem(text, SDL_strlen(text)), true, NULL);

    if (!sheet) {
        fprintf(stderr, "load failed: %s\n", SDL_GetError());
        failures++;
    }
    return sheet;
}

static void test_load_example(void)
{
    SPRAT_Sheet *sheet = SPRAT_LoadSheet(NULL, SPRAT_SDL_EXAMPLE_SHEET);
    const SPRAT_Frame *sword, *hero, *panel;
    const SPRAT_Animation *run, *left, *idle, *pingpong;
    int run01;

    CHECK(sheet != NULL);
    if (!sheet) {
        return;
    }

    CHECK_INT(sheet->num_atlases, 1);
    CHECK_STR(sheet->atlases[0].path, "atlas.png");
    CHECK_INT(sheet->atlases[0].w, 102);
    CHECK_INT(sheet->atlases[0].h, 134);
    CHECK(sheet->atlases[0].texture == NULL);   /* no renderer, no textures */
    CHECK_INT(sheet->num_frames, 16);
    CHECK_INT(sheet->num_animations, 5);
    CHECK_FLOAT(sheet->scale, 1.0);

    /* sprite 0 56 12 ... rotation 90, trim 0, pivot 6,50 */
    sword = &sheet->frames[0];
    CHECK_STR(sword->name, "sword");
    CHECK_STR(sword->path, "items/sword.png");
    CHECK_INT(sword->atlas, 0);
    CHECK_FLOAT(sword->source.w, 56);
    CHECK_FLOAT(sword->source.h, 12);
    CHECK_INT(sword->rotation, 90);
    CHECK_FLOAT(sword->w, 12);
    CHECK_FLOAT(sword->h, 56);
    CHECK_FLOAT(sword->content.w, 12);
    CHECK_FLOAT(sword->content.h, 56);
    CHECK_FLOAT(sword->pivot.x, 6);
    CHECK_FLOAT(sword->pivot.y, 50);

    /* run_01: 41x21 stored turned, trim 16,4 11,3 of a 48x48 image */
    run01 = SPRAT_GetFrameFromName(sheet, "hero/run_01.png");
    CHECK_INT(run01, 5);
    hero = &sheet->frames[run01];
    CHECK_FLOAT(hero->content.x, 16);
    CHECK_FLOAT(hero->content.y, 4);
    CHECK_FLOAT(hero->content.w, 21);
    CHECK_FLOAT(hero->content.h, 41);
    CHECK_FLOAT(hero->w, 48);
    CHECK_FLOAT(hero->h, 48);
    CHECK_INT(hero->num_markers, 3);
    CHECK_STR(hero->markers[2].name, "hitbox");
    CHECK_INT(hero->markers[2].type, SPRAT_MARKERTYPE_RECTANGLE);
    CHECK_FLOAT(hero->markers[2].position.x, 16);
    CHECK_FLOAT(hero->markers[2].h, 40);
    CHECK_INT(hero->markers[2].num_vertices, 0);
    CHECK(hero->markers[2].vertices == NULL);

    panel = &sheet->frames[SPRAT_GetFrameFromName(sheet, "panel")];
    CHECK_FLOAT(panel->slice.left, 8);
    CHECK_FLOAT(panel->slice.bottom, 8);
    CHECK_INT(panel->slice.horizontal_mode, SPRAT_SLICEMODE_STRETCH);
    CHECK_INT(panel->num_markers, 0);
    CHECK(panel->markers == NULL);

    run = SPRAT_GetAnimationFromName(sheet, "run");
    left = SPRAT_GetAnimationFromName(sheet, "run-left");
    CHECK(run == &sheet->animations[0]);
    CHECK(left != NULL);
    if (run && left) {
        CHECK_INT(run->fps, 10);
        CHECK_INT(run->num_frames, 6);
        CHECK_INT(run->frames[0], run01);
        CHECK_INT(run->flip, SDL_FLIP_NONE);
        CHECK(left->frames == run->frames);
        CHECK_INT(left->num_frames, 6);
        CHECK_INT(left->flip, SDL_FLIP_HORIZONTAL);
        CHECK_INT(run->loop_mode, SPRAT_LOOPMODE_REPEAT);
    }

    idle = SPRAT_GetAnimationFromName(sheet, "idle");      /* the run's first frame */
    CHECK(idle != NULL);
    if (idle) {
        CHECK_INT(idle->num_frames, 1);
        CHECK_INT(idle->frames[0], run01);
    }

    pingpong = SPRAT_GetAnimationFromName(sheet, "spin-pingpong");
    CHECK(pingpong != NULL);
    if (pingpong) {
        CHECK_INT(pingpong->loop_mode, SPRAT_LOOPMODE_PINGPONG);
        CHECK_INT(pingpong->num_frames, 5);
        CHECK_INT(pingpong->frames[4], SPRAT_GetFrameFromName(sheet, "coin/spin_05.png"));
    }

    SPRAT_DestroySheet(sheet);
}

/* Version 2 sheets give each animation a loop mode; an alias plays the way its source does,
   whatever mode it carries itself */
static void test_load_loop_modes(void)
{
    SPRAT_AnimationPlayer player;
    SPRAT_Sheet *sheet = load_text(
        "sprat-raw 2\n"
        "sheet 1 0 0 0 1 1 3 0 0 3 13 0: 0:\n"
        "atlas 16 16 0: 0: 0:\n"
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:s 1:s 0:\n"
        "anim 4 0 3 -1 2 0 0 3:opp\n"
        "anim 4 1 0 0 0 4:back\n"
        "anim 4 0 1 -1 1 0 4:once\n");

    if (!sheet) {
        return;
    }

    CHECK_INT(sheet->animations[0].loop_mode, SPRAT_LOOPMODE_ONCE_PINGPONG);
    CHECK_INT(sheet->animations[1].loop_mode, SPRAT_LOOPMODE_ONCE_PINGPONG);
    CHECK_INT(sheet->animations[1].flip, SDL_FLIP_HORIZONTAL);
    CHECK_INT(sheet->animations[2].loop_mode, SPRAT_LOOPMODE_ONCE);

    /* A player starts with its animation's mode */
    SPRAT_InitAnimationPlayer(&player, &sheet->animations[2]);
    CHECK_INT(player.loop_mode, SPRAT_LOOPMODE_ONCE);
    SPRAT_InitAnimationPlayer(&player, NULL);
    CHECK_INT(player.loop_mode, SPRAT_LOOPMODE_REPEAT);
    SPRAT_DestroySheet(sheet);
}

static void test_load_polygon_and_empty(void)
{
    SPRAT_AnimationPlayer empty;
    SPRAT_Sheet *sheet = load_text(
        "sprat-raw 1\n"
        "sheet 2 0 0 0 1 1 1 1 2 0 11 0: 0:\n"
        "atlas 16 16 0: 0: 0:\n"
        "sprite 0 0 0 8 8 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1:s 1:s 0:\n"
        "marker 3 3 4 0 0 0 2 3 4 1 2 4:poly\n"
        "anim 10 0 -1 0 5:empty\n");

    if (!sheet) {
        return;
    }

    CHECK_FLOAT(sheet->scale, 2.0);
    CHECK_INT(sheet->frames[0].markers[0].type, SPRAT_MARKERTYPE_POLYGON);
    CHECK_INT(sheet->frames[0].markers[0].num_vertices, 2);
    CHECK_FLOAT(sheet->frames[0].markers[0].vertices[0].x, 3);
    CHECK_FLOAT(sheet->frames[0].markers[0].vertices[1].y, 2);
    CHECK_STR(sheet->animations[0].name, "empty");
    CHECK_INT(sheet->animations[0].num_frames, 0);
    CHECK(sheet->animations[0].frames == NULL);

    /* An animation without frames plays nothing, and renders nothing without failing */
    SPRAT_InitAnimationPlayer(&empty, &sheet->animations[0]);
    SPRAT_UpdateAnimation(&empty, 1.0f);
    CHECK_FLOAT(empty.time, 0);
    CHECK_INT(SPRAT_GetAnimationFrame(&empty), -1);
    CHECK(!SPRAT_IsAnimationFinished(&empty));
    CHECK(SPRAT_RenderAnimation(NULL, sheet, &empty, 0.0f, 0.0f));
    SPRAT_DestroySheet(sheet);

    /* No sprites at all */
    sheet = load_text("sprat-raw 1\nsheet 1 0 0 0 1 0 0 0 0 0 0 0: 0:\natlas 4 4 0: 0: 0:\n");
    if (sheet) {
        CHECK_INT(sheet->num_frames, 0);
        CHECK(sheet->frames == NULL);
        CHECK(sheet->animations == NULL);
        CHECK_INT(sheet->num_atlases, 1);
        CHECK_STR(sheet->atlases[0].path, "");
        SPRAT_DestroySheet(sheet);
    }
}

/* Aliases share their source's frame indexes, so they can't multiply the size
   of the copy: on a 32-bit size_t, that size could wrap around and the copy
   overflow the allocation */
static void test_alias_frames_are_shared(void)
{
    enum { FRAMES = 1000, ALIASES = 1000 };
    size_t capacity = 256 + FRAMES * 2 + ALIASES * 32;
    size_t length;
    char *text;
    int i;

    /* run-back (0) aliases run (2) through run-alias (1), declared before both */
    SPRAT_Sheet *sheet = load_text(
        "sprat-raw 1\n"
        "sheet 1 0 0 0 1 2 3 0 0 3 24 0: 0:\n"
        "atlas 16 16 0: 0: 0:\n"
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:a 1:a 0:\n"
        "sprite 0 4 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:b 1:b 0:\n"
        "anim 5 1 1 0 8:run-back\n"
        "anim 5 0 2 0 9:run-alias\n"
        "anim 5 0 -1 3 1 0 1 3:run\n");

    if (sheet) {
        CHECK_INT(sheet->animations[2].num_frames, 3);
        CHECK(sheet->animations[0].frames == sheet->animations[2].frames);
        CHECK(sheet->animations[1].frames == sheet->animations[2].frames);
        CHECK_INT(sheet->animations[0].num_frames, 3);
        CHECK_INT(sheet->animations[0].frames[0], 1);
        CHECK_INT(sheet->animations[0].frames[1], 0);
        CHECK_INT(sheet->animations[0].flip, SDL_FLIP_HORIZONTAL);
        SPRAT_DestroySheet(sheet);
    }

    /* 1000 frames aliased 1000 times: a million indexes if each alias had its copy */
    text = (char *)SDL_malloc(capacity);
    length = (size_t)SDL_snprintf(text, capacity,
        "sprat-raw 1\nsheet 1 0 0 0 1 1 %d 0 0 %d %d 0: 0:\natlas 4 4 0: 0: 0:\n"
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:s 1:s 0:\nanim 10 0 -1 %d",
        ALIASES + 1, FRAMES, 2 + 1 + ALIASES * 4, FRAMES);
    for (i = 0; i < FRAMES; i++) {
        length += (size_t)SDL_snprintf(text + length, capacity - length, " 0");
    }
    length += (size_t)SDL_snprintf(text + length, capacity - length, " 1:r\n");
    for (i = 0; i < ALIASES; i++) {
        length += (size_t)SDL_snprintf(text + length, capacity - length, "anim 10 0 0 0 4:a%03d\n", i);
    }

    last_calloc_size = 0;
    sheet = load_text(text);
    if (sheet) {
        CHECK_INT(sheet->num_animations, ALIASES + 1);
        CHECK_INT(sheet->animations[ALIASES].num_frames, FRAMES);
        CHECK(sheet->animations[ALIASES].frames == sheet->animations[0].frames);
        CHECK(last_calloc_size > 0);
        CHECK(last_calloc_size < 256 * 1024);
        SPRAT_DestroySheet(sheet);
    }
    SDL_free(text);
}

static void test_add_size_overflow(void)
{
    size_t total = 0;

    CHECK(AddSize(&total, 3, 4, true));
    CHECK_INT(total, 16);
    CHECK(AddSize(&total, 5, 1, false));
    CHECK_INT(total, 21);

    /* A product or a sum past SIZE_MAX fails, leaving the total as it was */
    CHECK(!AddSize(&total, SIZE_MAX / 4 + 1, 4, false));
    CHECK(!AddSize(&total, SIZE_MAX - 10, 1, false));
    CHECK(!AddSize(&total, SIZE_MAX / 16, 16, true));  /* fits, but not once aligned */
    CHECK_INT(total, 21);
}

static void check_join(const char *dir, const char *path, const char *expected)
{
    char *joined = JoinPath(dir, path);

    CHECK_STR(joined, expected);
    SDL_free(joined);
}

static void check_directory(const char *file, const char *expected)
{
    char *dir = NULL;

    CHECK(GetDirectory(file, &dir));
    if (expected) {
        CHECK_STR(dir, expected);
    } else {
        CHECK(dir == NULL);
    }
    SDL_free(dir);
}

static void test_paths(void)
{
    check_join("res", "a.png", "res/a.png");
    check_join("res/", "a.png", "res/a.png");
    check_join("res\\", "a.png", "res\\a.png");
    check_join(".", "a.png", "a.png");
    check_join("", "a.png", "a.png");
    check_join(NULL, "a.png", "a.png");
    check_join("res", "/abs.png", "/abs.png");
    check_join("res", "C:\\a.png", "C:\\a.png");
    check_join("res", "sub/a.png", "res/sub/a.png");

    check_directory("res/sheet.sprat", "res");
    check_directory("a/b\\sheet.sprat", "a/b");
    check_directory("/sheet.sprat", "/");
    check_directory("sheet.sprat", NULL);
}

static void test_load_errors(void)
{
    static const char missing_atlas[] =
        "sprat-raw 1\nsheet 1 0 0 0 1 0 0 0 0 0 11 0: 0:\natlas 4 4 11:missing.png 0: 0:\n";
    static const char wrong_size_atlas[] =
        "sprat-raw 1\nsheet 1 0 0 0 1 0 0 0 0 0 9 0: 0:\natlas 4 4 9:atlas.png 0: 0:\n";
    SDL_Surface *target = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA8888);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(target);
    SPRAT_Sheet *sheet;
    char *resources = NULL;

    CHECK(SPRAT_LoadSheet(NULL, NULL) == NULL);
    CHECK_ERROR("file");
    CHECK(SPRAT_LoadSheet(NULL, "no/such/sheet.sprat") == NULL);
    CHECK_ERROR("Couldn't load sprite sheet no/such/sheet.sprat: ");
    CHECK(SPRAT_LoadSheet_IO(NULL, NULL, false, NULL) == NULL);
    CHECK_ERROR("src");

    /* Parse errors say where they are, and what is wrong */
    CHECK(SPRAT_LoadSheet_IO(NULL, SDL_IOFromConstMem("sprat-raw 1\nsheet x\n", 20), true, NULL) == NULL);
    CHECK_ERROR("Couldn't load sprite sheet from stream: 2:7: ");

    /* With a renderer, a missing atlas fails the load */
    CHECK(SPRAT_LoadSheet_IO(renderer, SDL_IOFromConstMem(missing_atlas, sizeof(missing_atlas) - 1), true, "res") == NULL);
    CHECK_ERROR("Couldn't load atlas res/missing.png: ");

    /* So does an atlas that is not the size the sheet was packed for */
    CHECK(GetDirectory(SPRAT_SDL_EXAMPLE_SHEET, &resources));
    CHECK(SPRAT_LoadSheet_IO(renderer, SDL_IOFromConstMem(wrong_size_atlas, sizeof(wrong_size_atlas) - 1), true, resources) == NULL);
    CHECK_ERROR("is 102x134, the sprite sheet was packed for 4x4");

    /* Without one, the atlas is not even looked for */
    sheet = SPRAT_LoadSheet_IO(NULL, SDL_IOFromConstMem(missing_atlas, sizeof(missing_atlas) - 1), true, "res");
    CHECK(sheet != NULL);
    SPRAT_DestroySheet(sheet);

    SPRAT_DestroySheet(NULL);   /* a no-op */

    SDL_free(resources);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(target);
}

/* The sheet's enums are cast to sprat-sdl's, so their values must match */
static void test_enums_match_sprat(void)
{
    CHECK_INT(SPRAT_MARKERTYPE_POINT, SPRAT_MARKER_POINT);
    CHECK_INT(SPRAT_MARKERTYPE_CIRCLE, SPRAT_MARKER_CIRCLE);
    CHECK_INT(SPRAT_MARKERTYPE_RECTANGLE, SPRAT_MARKER_RECTANGLE);
    CHECK_INT(SPRAT_MARKERTYPE_POLYGON, SPRAT_MARKER_POLYGON);
    CHECK_INT(SPRAT_SLICEMODE_STRETCH, SPRAT_SLICE_STRETCH);
    CHECK_INT(SPRAT_SLICEMODE_REPEAT, SPRAT_SLICE_REPEAT);
    CHECK_INT(SPRAT_SLICEMODE_MIRROR, SPRAT_SLICE_MIRROR);
    CHECK_INT(SPRAT_SLICEMODE_FIXED, SPRAT_SLICE_FIXED);
    CHECK_INT(SDL_FLIP_HORIZONTAL, SPRAT_FLIP_H);
    CHECK_INT(SDL_FLIP_VERTICAL, SPRAT_FLIP_V);
    CHECK_INT(SPRAT_LOOPMODE_ONCE, SPRAT_LOOP_ONCE);
    CHECK_INT(SPRAT_LOOPMODE_PINGPONG, SPRAT_LOOP_PINGPONG);
}

static void test_load_slices_and_markers(void)
{
    /* A point marker, and every slice mode but stretch, each inset different */
    SPRAT_Sheet *sheet = load_text(
        "sprat-raw 1\n"
        "sheet 1 0 0 0 1 2 0 1 0 0 7 0: 0:\n"
        "atlas 16 16 0: 0: 0:\n"
        "sprite 0 0 0 8 8 0 0 0 0 0 0 0 1 1 2 3 4 1 3 1 1:s 1:s 0:\n"
        "marker 0 5 6 0 0 0 0 3:hit\n"
        "sprite 0 8 0 8 8 0 0 0 0 0 0 0 1 1 1 1 1 2 0 0 1:t 1:t 0:\n");
    const SPRAT_Frame *frame;

    if (!sheet) {
        return;
    }

    frame = &sheet->frames[0];
    CHECK_FLOAT(frame->slice.left, 1);
    CHECK_FLOAT(frame->slice.top, 2);
    CHECK_FLOAT(frame->slice.right, 3);
    CHECK_FLOAT(frame->slice.bottom, 4);
    CHECK_INT(frame->slice.horizontal_mode, SPRAT_SLICEMODE_REPEAT);
    CHECK_INT(frame->slice.vertical_mode, SPRAT_SLICEMODE_FIXED);
    CHECK_INT(frame->markers[0].type, SPRAT_MARKERTYPE_POINT);
    CHECK_FLOAT(frame->markers[0].position.x, 5);
    CHECK_FLOAT(frame->markers[0].position.y, 6);
    CHECK(frame->markers[0].vertices == NULL);

    frame = &sheet->frames[1];
    CHECK_INT(frame->slice.horizontal_mode, SPRAT_SLICEMODE_MIRROR);
    CHECK_INT(frame->slice.vertical_mode, SPRAT_SLICEMODE_STRETCH);
    CHECK_INT(frame->num_markers, 0);

    SPRAT_DestroySheet(sheet);
}

static void test_load_atlases(void)
{
    /* Atlas 1 of two: frames keep their atlas index, and every atlas gets a texture */
    static const char two_atlases[] =
        "sprat-raw 1\nsheet 1 0 0 0 2 1 0 0 0 0 22 0: 0:\n"
        "atlas 102 134 9:atlas.png 0: 0:\natlas 102 134 11:./atlas.png 0: 0:\n"
        "sprite 1 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:s 1:s 0:\n";
    /* The second atlas is missing: the first one's texture must not leak */
    static const char second_missing[] =
        "sprat-raw 1\nsheet 1 0 0 0 2 0 0 0 0 0 20 0: 0:\n"
        "atlas 102 134 9:atlas.png 0: 0:\natlas 4 4 11:missing.png 0: 0:\n";
    SDL_Surface *surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA8888);
    SDL_Renderer *software = SDL_CreateSoftwareRenderer(surface);
    SDL_IOStream *io;
    SPRAT_Sheet *sheet;
    char *resources = NULL;
    int allocations, i;

    CHECK(GetDirectory(SPRAT_SDL_EXAMPLE_SHEET, &resources));

    sheet = SPRAT_LoadSheet_IO(software, SDL_IOFromConstMem(two_atlases, sizeof(two_atlases) - 1), true, resources);
    CHECK(sheet != NULL);
    if (sheet) {
        CHECK_INT(sheet->num_atlases, 2);
        CHECK_STR(sheet->atlases[1].path, "./atlas.png");
        CHECK(sheet->atlases[0].texture != NULL);
        CHECK(sheet->atlases[1].texture != NULL);
        CHECK(sheet->atlases[0].texture != sheet->atlases[1].texture);
        CHECK_INT(sheet->frames[0].atlas, 1);
        CHECK(SPRAT_RenderFrame(software, sheet, 0, 0.0f, 0.0f));
        SPRAT_DestroySheet(sheet);
    }

    /* The renderer would free a leaked texture with itself, so count SDL's allocations instead.
       The first load sizes the error buffer, which the count would otherwise see grow */
    for (i = 0; i < 2; i++) {
        allocations = live_allocations;
        CHECK(SPRAT_LoadSheet_IO(software, SDL_IOFromConstMem(second_missing, sizeof(second_missing) - 1), true, resources) == NULL);
        CHECK_ERROR("Couldn't load atlas ");
        CHECK_ERROR("missing.png: ");
    }
    CHECK_INT(live_allocations, allocations);

    /* Without closeio the stream stays open for the caller, loaded or not */
    io = SDL_IOFromConstMem(two_atlases, sizeof(two_atlases) - 1);
    sheet = SPRAT_LoadSheet_IO(NULL, io, false, NULL);
    CHECK(sheet != NULL);
    CHECK_INT(SDL_TellIO(io), sizeof(two_atlases) - 1);
    CHECK(SDL_CloseIO(io));
    SPRAT_DestroySheet(sheet);

    io = SDL_IOFromConstMem("sprat-raw 2\n", 12);
    CHECK(SPRAT_LoadSheet_IO(NULL, io, false, NULL) == NULL);
    CHECK(SDL_CloseIO(io));

    SDL_free(resources);
    SDL_DestroyRenderer(software);
    SDL_DestroySurface(surface);
}

static void test_load_out_of_memory(void)
{
    static const char text[] =
        "sprat-raw 1\nsheet 1 0 0 0 1 1 0 0 0 0 2 0: 0:\natlas 4 4 0: 0: 0:\n"
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:s 1:s 0:\n";
    sprat_malloc_func sprat_malloc;
    sprat_calloc_func sprat_calloc;
    sprat_realloc_func sprat_realloc;
    sprat_free_func sprat_free;
    SDL_IOStream *io;

    /* sprat-sdl's copy of the sheet can't be allocated */
    io = SDL_IOFromConstMem(text, sizeof(text) - 1);
    fail_callocs = true;
    CHECK(SPRAT_LoadSheet_IO(NULL, io, true, NULL) == NULL);
    fail_callocs = false;
    CHECK_ERROR("Out of memory");

    /* Nor can sprat-lib's sheet */
    sprat_get_memory_functions(&sprat_malloc, &sprat_calloc, &sprat_realloc, &sprat_free);
    CHECK(sprat_set_memory_functions(failing_malloc, failing_calloc, sprat_realloc, sprat_free));
    SDL_ClearError();
    CHECK(SPRAT_LoadSheet_IO(NULL, SDL_IOFromConstMem(text, sizeof(text) - 1), true, NULL) == NULL);
    CHECK(sprat_set_memory_functions(sprat_malloc, sprat_calloc, sprat_realloc, sprat_free));
    CHECK_ERROR("Out of memory");
}

/* ---- Queries -------------------------------------------------------------- */

static void test_frame_names(void)
{
    /* Frame 0 is named like frame 1's path, and frame 2's name and path hold control characters */
    SPRAT_Sheet *sheet = load_text(
        "sprat-raw 1\n"
        "sheet 1 0 0 0 1 3 0 0 0 0 30 0: 0:\n"
        "atlas 16 16 0: 0: 0:\n"
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 5:b.png 5:a.png 0:\n"
        "sprite 0 4 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1:b 5:b.png 0:\n"
        "sprite 0 8 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 5:\x1b[31m 9:\x1b[31m.png 0:\n");

    if (!sheet) {
        return;
    }

    /* A path is unique, so it wins over a name, even an earlier frame's */
    CHECK_INT(SPRAT_GetFrameFromName(sheet, "b.png"), 1);
    CHECK_INT(SPRAT_GetFrameFromName(sheet, "a.png"), 0);
    CHECK_INT(SPRAT_GetFrameFromName(sheet, "b"), 1);
    CHECK_INT(SPRAT_GetFrameFromName(sheet, "\x1b[31m"), 2);
    CHECK_INT(SPRAT_GetFrameFromName(sheet, ""), -1);

    /* Names in error messages are escaped, so they can't take over a terminal */
    CHECK_INT(SPRAT_GetFrameFromName(sheet, "\x1b[32m"), -1);
    CHECK_ERROR("Couldn't find frame \\x1B[32m");
    CHECK(SDL_strchr(SDL_GetError(), '\x1b') == NULL);
    CHECK(SPRAT_GetAnimationFromName(sheet, "\x1b[32m") == NULL);
    CHECK_ERROR("Couldn't find animation \\x1B[32m");
    CHECK(SDL_strchr(SDL_GetError(), '\x1b') == NULL);
    CHECK(SPRAT_GetFrameMarker(sheet, 2, "\x1b[32m") == NULL);
    CHECK_ERROR("Frame \\x1B[31m.png has no marker \\x1B[32m");
    CHECK(SDL_strchr(SDL_GetError(), '\x1b') == NULL);

    SPRAT_DestroySheet(sheet);
}

static void test_lookups(void)
{
    static const float times[] = { 0.0f, 0.24f, 0.25f, 0.5f, 0.75f, -0.1f, 3600.0f * 24.0f * 365.0f };
    static const int expected[] = { 2, 2, 0, 1, 2, 1, 2 };     /* a year is 4*31536000 steps, a multiple of 3 */
    SPRAT_AnimationPlayer walk, back, none;
    const SPRAT_Marker *hit;
    size_t i;
    SPRAT_Sheet *sheet = load_text(
        "sprat-raw 1\n"
        "sheet 1 0 0 0 1 3 2 1 0 3 53 0: 0:\n"
        "atlas 16 16 0: 0: 0:\n"
        "sprite 0 0 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 4:idle 10:a/idle.png 0:\n"
        "marker 1 2 3 4 0 0 0 3:hit\n"
        "sprite 0 4 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 4:idle 10:b/idle.png 0:\n"
        "sprite 0 8 0 4 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 4:jump 10:b/jump.png 0:\n"
        "anim 4 0 -1 3 2 0 1 4:walk\n"
        "anim 4 3 0 0 4:back\n");

    if (!sheet) {
        return;
    }

    CHECK_INT(SPRAT_GetFrameFromName(sheet, "a/idle.png"), 0);
    CHECK_INT(SPRAT_GetFrameFromName(sheet, "b/idle.png"), 1);
    CHECK_INT(SPRAT_GetFrameFromName(sheet, "jump"), 2);           /* a unique name */
    CHECK_INT(SPRAT_GetFrameFromName(sheet, "idle"), -1);          /* ambiguous: two folders */
    CHECK_ERROR("ambiguous");
    CHECK_INT(SPRAT_GetFrameFromName(sheet, "missing"), -1);
    CHECK_ERROR("Couldn't find frame missing");
    CHECK_INT(SPRAT_GetFrameFromName(sheet, NULL), -1);
    CHECK_INT(SPRAT_GetFrameFromName(NULL, "jump"), -1);

    CHECK_STR(SPRAT_GetAnimationFromName(sheet, "back")->name, "back");
    CHECK(SPRAT_GetAnimationFromName(sheet, "run") == NULL);
    CHECK_ERROR("Couldn't find animation run");
    CHECK(SPRAT_GetAnimationFromName(sheet, NULL) == NULL);
    CHECK_ERROR("name");
    CHECK(SPRAT_GetAnimationFromName(NULL, "back") == NULL);
    CHECK_ERROR("sheet");
    CHECK_INT(sheet->animations[1].flip, SDL_FLIP_HORIZONTAL_AND_VERTICAL);
    CHECK_INT(sheet->animations[0].loop_mode, SPRAT_LOOPMODE_REPEAT);   /* version 1 sheets have no loop modes */

    hit = SPRAT_GetFrameMarker(sheet, 0, "hit");
    CHECK(hit != NULL);
    if (hit) {
        CHECK_INT(hit->type, SPRAT_MARKERTYPE_CIRCLE);
        CHECK_FLOAT(hit->radius, 4);
    }
    CHECK(SPRAT_GetFrameMarker(sheet, 0, "pivot") == NULL);
    CHECK_ERROR("Frame a/idle.png has no marker pivot");
    CHECK(SPRAT_GetFrameMarker(sheet, 3, "hit") == NULL);
    CHECK_ERROR("Frame 3 is out of range");
    CHECK(SPRAT_GetFrameMarker(sheet, -1, "hit") == NULL);
    CHECK_ERROR("Frame -1 is out of range");
    CHECK(SPRAT_GetFrameMarker(sheet, 1, "hit") == NULL);  /* frame 1 has no markers at all */
    CHECK_ERROR("Frame b/idle.png has no marker hit");
    CHECK(SPRAT_GetFrameMarker(sheet, 0, NULL) == NULL);
    CHECK_ERROR("name");
    CHECK(SPRAT_GetFrameMarker(NULL, 0, "hit") == NULL);
    CHECK_ERROR("sheet");

    /* A sprite's marker is its frame's */
    {
        SPRAT_Sprite sprite = { NULL, 0, 10.0f, 20.0f, 2.0f, 2.0f, 45.0, SDL_FLIP_HORIZONTAL };

        sprite.sheet = sheet;
        CHECK(SPRAT_GetSpriteMarker(&sprite, "hit") == hit);
        sprite.frame = 1;
        CHECK(SPRAT_GetSpriteMarker(&sprite, "hit") == NULL);
        CHECK_ERROR("Frame b/idle.png has no marker hit");
        sprite.frame = 3;
        CHECK(SPRAT_GetSpriteMarker(&sprite, "hit") == NULL);
        CHECK_ERROR("Frame 3 is out of range");
        sprite.frame = 0;
        CHECK(SPRAT_GetSpriteMarker(&sprite, NULL) == NULL);
        CHECK_ERROR("name");
        sprite.sheet = NULL;
        CHECK(SPRAT_GetSpriteMarker(&sprite, "hit") == NULL);
        CHECK_ERROR("sheet");
        CHECK(SPRAT_GetSpriteMarker(NULL, "hit") == NULL);
        CHECK_ERROR("sprite");
    }

    /* walk plays frames 2, 0, 1 at 4 fps, looping */
    SPRAT_InitAnimationPlayer(&walk, &sheet->animations[0]);
    for (i = 0; i < COUNT(times); i++) {
        walk.time = times[i];
        CHECK_INT(SPRAT_GetAnimationFrame(&walk), expected[i]);
    }
    SPRAT_InitAnimationPlayer(&back, &sheet->animations[1]);
    back.time = 0.25f;
    CHECK_INT(SPRAT_GetAnimationFrame(&back), 0);
    SPRAT_InitAnimationPlayer(&none, SPRAT_GetAnimationFromName(sheet, "missing"));
    CHECK_INT(SPRAT_GetAnimationFrame(&none), -1);

    SPRAT_DestroySheet(sheet);
}

/* Frames 7, 8, 9 at 4 fps: each lasts 0.25 s, exact in binary, and the
   animation lasts 0.75 s */
static const int walk_frames[] = { 7, 8, 9 };
static const SPRAT_Animation walk_animation = { "walk", 4, walk_frames, 3, SDL_FLIP_NONE, SPRAT_LOOPMODE_REPEAT };

static void test_animation_player(void)
{
    SPRAT_AnimationPlayer player;

    SPRAT_InitAnimationPlayer(&player, &walk_animation);
    CHECK(player.playing);
    CHECK_INT(player.loop_mode, SPRAT_LOOPMODE_REPEAT);
    CHECK_FLOAT(player.speed, 1);
    CHECK_FLOAT(player.time, 0);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);

    /* Looping forward: each frame from its start, and back to the first */
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 8);
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 9);
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_FLOAT(player.time, 0);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);
    SPRAT_UpdateAnimation(&player, 7.5f + 0.25f);    /* many loops at once: time stays wrapped */
    CHECK_FLOAT(player.time, 0.25f);
    CHECK(!SPRAT_IsAnimationFinished(&player));

    /* Speed scales the step */
    player.speed = 2.0f;
    SPRAT_UpdateAnimation(&player, 0.125f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 9);

    /* Paused, it keeps its time and frame */
    SPRAT_PauseAnimation(&player);
    CHECK(!player.playing);
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_FLOAT(player.time, 0.5f);
    SPRAT_ResumeAnimation(&player);
    SPRAT_UpdateAnimation(&player, 0.125f);
    CHECK_FLOAT(player.time, 0.0f);

    /* Bad steps and speeds keep the last good time */
    SPRAT_UpdateAnimation(&player, NAN);
    SPRAT_UpdateAnimation(&player, INFINITY);
    player.speed = NAN;
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_FLOAT(player.time, 0.0f);
    SPRAT_UpdateAnimation(NULL, 1.0f);

    /* Reverse: starts on the last frame, each frame from its end, wrapping around */
    SPRAT_InitAnimationPlayer(&player, &walk_animation);
    player.speed = -1.0f;
    SPRAT_PlayAnimation(&player);
    CHECK_FLOAT(player.time, 0.75f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 9);
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 8);
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);
    SPRAT_UpdateAnimation(&player, 0.125f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);
    SPRAT_UpdateAnimation(&player, 0.125f);
    CHECK_FLOAT(player.time, 0);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 9);  /* time 0 ends frame 7, so it is the wrap to 9 */
    SPRAT_UpdateAnimation(&player, 0.125f);
    CHECK_FLOAT(player.time, 0.625f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 9);

    /* Once: stops on the last frame and reports it finished */
    SPRAT_InitAnimationPlayer(&player, &walk_animation);
    player.loop_mode = SPRAT_LOOPMODE_ONCE;
    SPRAT_UpdateAnimation(&player, 0.5f);
    CHECK(player.playing);
    CHECK(!SPRAT_IsAnimationFinished(&player));
    SPRAT_UpdateAnimation(&player, 1.0f);
    CHECK(!player.playing);
    CHECK(SPRAT_IsAnimationFinished(&player));
    CHECK_FLOAT(player.time, 0.75f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 9);
    SPRAT_PlayAnimation(&player);
    SPRAT_UpdateAnimation(&player, 0.0f);            /* a zero step doesn't stop a fresh one */
    CHECK(player.playing);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);

    /* Once in reverse: stops on the first frame */
    player.speed = -1.0f;
    SPRAT_PlayAnimation(&player);
    SPRAT_UpdateAnimation(&player, 2.0f);
    CHECK(!player.playing);
    CHECK(SPRAT_IsAnimationFinished(&player));
    CHECK_FLOAT(player.time, 0);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);

    /* Stop rewinds to where Play starts, without playing */
    SPRAT_StopAnimation(&player);
    CHECK(!player.playing);
    CHECK_FLOAT(player.time, 0.75f);
    player.speed = 1.0f;
    SPRAT_StopAnimation(&player);
    CHECK_FLOAT(player.time, 0);

    /* Stopped, updates don't move it; resumed, it plays on from the start */
    SPRAT_InitAnimationPlayer(&player, &walk_animation);
    SPRAT_UpdateAnimation(&player, 0.5f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 9);
    SPRAT_StopAnimation(&player);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_FLOAT(player.time, 0);
    CHECK(!player.playing);
    SPRAT_ResumeAnimation(&player);
    CHECK(player.playing);
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 8);

    /* Play restarts a finished animation that plays once */
    player.loop_mode = SPRAT_LOOPMODE_ONCE;
    SPRAT_UpdateAnimation(&player, 1.0f);
    CHECK(SPRAT_IsAnimationFinished(&player));
    SPRAT_PlayAnimation(&player);
    CHECK(!SPRAT_IsAnimationFinished(&player));
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 8);

    /* Resuming a finished one doesn't play it again: it is still at the end */
    SPRAT_UpdateAnimation(&player, 1.0f);
    SPRAT_ResumeAnimation(&player);
    SPRAT_UpdateAnimation(&player, 0.25f);
    CHECK(!player.playing);
    CHECK_FLOAT(player.time, 0.75f);

    /* At speed 0 it holds its frame, without finishing */
    SPRAT_PlayAnimation(&player);
    SPRAT_UpdateAnimation(&player, 0.25f);
    player.speed = 0.0f;
    SPRAT_UpdateAnimation(&player, 10.0f);
    CHECK(player.playing);
    CHECK_FLOAT(player.time, 0.25f);
    CHECK(!SPRAT_IsAnimationFinished(&player));

    /* Times set by hand: any value maps to a frame, or to none */
    SPRAT_InitAnimationPlayer(&player, &walk_animation);
    player.time = -0.1f;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 9);
    player.time = 100.0f;                           /* step 400, past 133 loops */
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 8);
    player.time = NAN;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), -1);
    player.time = INFINITY;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), -1);
    player.loop_mode = SPRAT_LOOPMODE_ONCE;
    player.time = 100.0f;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 9);
    player.time = -100.0f;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);

    /* No animation (not found) plays nothing */
    SPRAT_InitAnimationPlayer(&player, NULL);
    SPRAT_UpdateAnimation(&player, 1.0f);
    SPRAT_PlayAnimation(&player);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), -1);
    CHECK(!SPRAT_IsAnimationFinished(&player));
    CHECK(!SPRAT_RenderAnimation(NULL, NULL, NULL, 0.0f, 0.0f));
    CHECK_ERROR("player");

    /* Nor does one without a frame rate, which a sheet can't have but a program could make */
    {
        static const SPRAT_Animation still = { "still", 0, walk_frames, 3, SDL_FLIP_NONE, SPRAT_LOOPMODE_REPEAT };

        SPRAT_InitAnimationPlayer(&player, &still);
        SPRAT_UpdateAnimation(&player, 1.0f);
        CHECK_FLOAT(player.time, 0);
        CHECK_INT(SPRAT_GetAnimationFrame(&player), -1);
        player.loop_mode = SPRAT_LOOPMODE_ONCE;
        CHECK(!SPRAT_IsAnimationFinished(&player));
    }

    /* Without a player, every call is a no-op */
    SPRAT_InitAnimationPlayer(NULL, &walk_animation);
    SPRAT_PlayAnimation(NULL);
    SPRAT_StopAnimation(NULL);
    SPRAT_PauseAnimation(NULL);
    SPRAT_ResumeAnimation(NULL);
    CHECK(!SPRAT_IsAnimationFinished(NULL));
    CHECK_INT(SPRAT_GetAnimationFrame(NULL), -1);
}

/* Checks the frames a player shows, updating it by one frame (0.25 s) after each */
#define CHECK_FRAMES(player, expected) check_frames(__LINE__, (player), (expected), COUNT(expected))

static void check_frames(int line, SPRAT_AnimationPlayer *player, const int *expected, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        int frame = SPRAT_GetAnimationFrame(player);

        if (frame != expected[i]) {
            fprintf(stderr, "%s:%d: frame %u is %d, expected %d\n", __FILE__, line, (unsigned)i, frame, expected[i]);
            failures++;
        }
        SPRAT_UpdateAnimation(player, 0.25f);
    }
}

/* The first num_frames of walk_animation, played with a loop mode */
static void init_walk_player(SPRAT_AnimationPlayer *player, SPRAT_Animation *animation, SPRAT_LoopMode loop_mode, int num_frames)
{
    *animation = walk_animation;
    animation->num_frames = num_frames;
    SPRAT_InitAnimationPlayer(player, animation);
    player->loop_mode = loop_mode;
}

/* Setting the animation played restarts only on a change, keeping the speed */
static void test_set_animation(void)
{
    static const int idle_frames[] = { 3 };
    SPRAT_Animation idle = walk_animation;
    SPRAT_Animation once = walk_animation;
    SPRAT_Animation shorter = walk_animation;
    SPRAT_AnimationPlayer player;

    idle.name = "idle";
    idle.frames = idle_frames;
    idle.num_frames = 1;
    idle.loop_mode = SPRAT_LOOPMODE_ONCE;
    once.loop_mode = SPRAT_LOOPMODE_ONCE;
    once.flip = SDL_FLIP_VERTICAL;
    shorter.num_frames = 2;

    SPRAT_InitAnimationPlayer(&player, &walk_animation);
    player.speed = 2.0f;
    SPRAT_UpdateAnimation(&player, 0.125f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 8);

    player.loop_mode = SPRAT_LOOPMODE_PINGPONG;
    SPRAT_SetAnimation(&player, &walk_animation);   /* the one played: nothing changes */
    CHECK_FLOAT(player.time, 0.25f);
    CHECK_INT(player.loop_mode, SPRAT_LOOPMODE_PINGPONG);
    SPRAT_SetAnimation(&player, &once);             /* a copy is another one: replaced */
    CHECK(player.animation == &once);
    CHECK_FLOAT(player.time, 0);
    CHECK_INT(player.loop_mode, SPRAT_LOOPMODE_ONCE);

    SPRAT_SetAnimation(&player, &idle);             /* another one: replaced, from its start */
    CHECK(player.animation == &idle);
    CHECK_FLOAT(player.time, 0);
    CHECK_FLOAT(player.speed, 2);
    CHECK_INT(player.loop_mode, SPRAT_LOOPMODE_ONCE);   /* its own loop mode */
    CHECK(player.playing);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 3);

    SPRAT_SetAnimation(&player, &walk_animation);
    CHECK_INT(player.loop_mode, SPRAT_LOOPMODE_REPEAT);
    SPRAT_UpdateAnimation(&player, 0.125f);
    SPRAT_SetAnimation(&player, &shorter);          /* another frame count: replaced */
    CHECK(player.animation == &shorter);
    CHECK_FLOAT(player.time, 0);

    SPRAT_StopAnimation(&player);
    SPRAT_SetAnimation(&player, &shorter);          /* stopped stays stopped */
    CHECK(!player.playing);
    SPRAT_SetAnimation(&player, &walk_animation);   /* a new one plays */
    CHECK(player.playing);

    player.speed = -1.0f;
    SPRAT_SetAnimation(&player, &idle);             /* reversed: from its end */
    CHECK_FLOAT(player.time, 0.25f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 3);

    SPRAT_SetAnimation(&player, NULL);              /* nothing played */
    CHECK(player.animation == NULL);
    CHECK_INT(player.loop_mode, SPRAT_LOOPMODE_REPEAT);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), -1);
    SPRAT_SetAnimation(&player, NULL);
    SPRAT_SetAnimation(&player, &walk_animation);
    CHECK(player.animation == &walk_animation);

    SPRAT_SetAnimation(NULL, &walk_animation);      /* ignored */
}

/* A sprite follows its player's frame and flip; the rendered animation sprite combines the
   given transform with them */
static void test_animation_sprite(void)
{
    SPRAT_Animation mirrored = walk_animation;
    SPRAT_AnimationPlayer player, none;
    SPRAT_Sprite sprite = { NULL, 0, 5.0f, 6.0f, -2.0f, 3.0f, 45.0, SDL_FLIP_VERTICAL };
    SPRAT_Sprite rendered;

    mirrored.flip = SDL_FLIP_HORIZONTAL;
    SPRAT_InitAnimationPlayer(&player, &mirrored);

    SPRAT_SetSpriteAnimationFrame(&sprite, &player);
    CHECK_INT(sprite.frame, 7);
    CHECK_INT(sprite.flip, SDL_FLIP_HORIZONTAL);    /* replaced, not combined */
    CHECK_FLOAT(sprite.x, 5);                       /* the rest is the program's */
    CHECK_FLOAT(sprite.scale_x, -2);
    CHECK_FLOAT(sprite.angle, 45);

    SPRAT_UpdateAnimation(&player, 0.5f);
    SPRAT_SetSpriteAnimationFrame(&sprite, &player);
    CHECK_INT(sprite.frame, 9);
    SPRAT_SetSpriteAnimationFrame(&sprite, &player);    /* idempotent */
    CHECK_INT(sprite.frame, 9);
    CHECK_INT(sprite.flip, SDL_FLIP_HORIZONTAL);

    SPRAT_SetSpriteAnimationFrame(NULL, &player);   /* ignored */
    SPRAT_InitAnimationPlayer(&none, NULL);
    SPRAT_SetSpriteAnimationFrame(&sprite, &none);
    CHECK_INT(sprite.frame, -1);                    /* nothing shown */
    CHECK_INT(sprite.flip, SDL_FLIP_NONE);
    sprite.frame = 0;
    SPRAT_SetSpriteAnimationFrame(&sprite, NULL);
    CHECK_INT(sprite.frame, -1);

    rendered = MakeAnimationSprite(NULL, &player, 1.0f, 2.0f, 3.0f, 4.0f, 30.0,
                                   (SDL_FlipMode)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
    CHECK_INT(rendered.frame, 9);
    CHECK_INT(rendered.flip, SDL_FLIP_VERTICAL);    /* the animation's horizontal flip undone */
    CHECK_FLOAT(rendered.x, 1);
    CHECK_FLOAT(rendered.y, 2);
    CHECK_FLOAT(rendered.scale_x, 3);
    CHECK_FLOAT(rendered.scale_y, 4);
    CHECK_FLOAT(rendered.angle, 30);

    rendered = MakeAnimationSprite(NULL, &player, 1.0f, 2.0f, 1.0f, 1.0f, 0.0, SDL_FLIP_NONE);
    CHECK_INT(rendered.flip, SDL_FLIP_HORIZONTAL);  /* SPRAT_RenderAnimation(): the animation's flip */

    SPRAT_UpdateSpriteAnimation(&sprite, &player, 0.25f);  /* advanced, then set: wraps to 7 */
    CHECK_INT(sprite.frame, 7);
    CHECK_FLOAT(player.time, 0);
    sprite.flip = SDL_FLIP_NONE;
    SPRAT_StopAnimation(&player);
    SPRAT_UpdateAnimation(&player, 0.25f);
    SPRAT_UpdateSpriteAnimation(&sprite, &player, 0.0f);   /* only sets the sprite */
    CHECK_INT(sprite.frame, 7);
    CHECK_INT(sprite.flip, SDL_FLIP_HORIZONTAL);
    SPRAT_ResumeAnimation(&player);
    SPRAT_UpdateSpriteAnimation(NULL, &player, 0.25f);     /* no sprite: the player still advances */
    CHECK_FLOAT(player.time, 0.25f);
    SPRAT_UpdateSpriteAnimation(&sprite, NULL, 0.25f);     /* ignored */
    CHECK_INT(sprite.frame, 7);
}

static void test_animation_pingpong(void)
{
    static const int forward[] = { 7, 8, 9, 8, 7, 8, 9, 8, 7 };
    static const int backward[] = { 8, 9, 8, 7, 8, 9 };
    static const int once[] = { 7, 8, 9, 8, 7 };
    SPRAT_Animation animation;
    SPRAT_AnimationPlayer player;

    /* Repeating: 7 8 9 8, then 7 again; one pass lasts 1 s */
    init_walk_player(&player, &animation, SPRAT_LOOPMODE_PINGPONG, 3);
    CHECK_FRAMES(&player, forward);
    CHECK(player.playing);
    CHECK(!SPRAT_IsAnimationFinished(&player));
    CHECK_FLOAT(player.time, 0.25f);

    /* In reverse: the same pass backwards, from its last step */
    player.speed = -1.0f;
    SPRAT_PlayAnimation(&player);
    CHECK_FLOAT(player.time, 1.0f);
    CHECK_FRAMES(&player, backward);

    /* Once: 7 8 9 8 7, stopping on the first frame after 1.25 s */
    init_walk_player(&player, &animation, SPRAT_LOOPMODE_ONCE_PINGPONG, 3);
    CHECK_FRAMES(&player, once);
    CHECK(!player.playing);
    CHECK(SPRAT_IsAnimationFinished(&player));
    CHECK_FLOAT(player.time, 1.25f);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);
    player.time = 100.0f;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);

    /* Once in reverse: the same frames, stopping on the first frame at time 0 */
    player.speed = -1.0f;
    SPRAT_PlayAnimation(&player);
    CHECK_FRAMES(&player, once);    /* a palindrome */
    CHECK(SPRAT_IsAnimationFinished(&player));
    CHECK_FLOAT(player.time, 0);
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);

    /* Two frames: 7 8 repeating, or 7 8 7 once; one frame has no way back */
    init_walk_player(&player, &animation, SPRAT_LOOPMODE_PINGPONG, 2);
    CHECK_FLOAT(GetAnimationDuration(&player), 0.5f);
    player.time = 0.5f;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);
    init_walk_player(&player, &animation, SPRAT_LOOPMODE_ONCE_PINGPONG, 2);
    CHECK_FLOAT(GetAnimationDuration(&player), 0.75f);
    player.time = 0.5f;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);
    player.time = 0.25f;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 8);
    init_walk_player(&player, &animation, SPRAT_LOOPMODE_PINGPONG, 1);
    CHECK_FLOAT(GetAnimationDuration(&player), 0.25f);
    player.time = 0.6f;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 7);

    /* The player's mode wins over its animation's */
    init_walk_player(&player, &animation, SPRAT_LOOPMODE_REPEAT, 3);
    animation.loop_mode = SPRAT_LOOPMODE_ONCE;
    player.time = 1.0f;
    CHECK_INT(SPRAT_GetAnimationFrame(&player), 8);

    /* The pass of the largest frame count doesn't overflow */
    {
        static const SPRAT_Animation huge = { "huge", 1, walk_frames, SDL_MAX_SINT32, SDL_FLIP_NONE, SPRAT_LOOPMODE_ONCE_PINGPONG };

        SPRAT_InitAnimationPlayer(&player, &huge);
        CHECK(GetAnimationStepCount(&player) == 2.0 * SDL_MAX_SINT32 - 1.0);
    }
}

/* ---- Marker overlaps ------------------------------------------------------ */

/* Two frames to place markers with: frame 0's pivot is its top-left, frame 1's is (3, -2) */
static SPRAT_Frame overlap_frames[2];
static SPRAT_Sheet overlap_sheet;

static void make_overlap_sheet(void)
{
    SDL_zeroa(overlap_frames);
    overlap_frames[1].pivot.x = 3.0f;
    overlap_frames[1].pivot.y = -2.0f;
    SDL_zero(overlap_sheet);
    overlap_sheet.num_frames = 2;
    overlap_sheet.frames = overlap_frames;
    overlap_sheet.scale = 1.0f;
}

static SPRAT_Marker point_marker(float x, float y)
{
    SPRAT_Marker m;

    SDL_zero(m);
    m.name = "point";
    m.type = SPRAT_MARKERTYPE_POINT;
    m.position.x = x;
    m.position.y = y;
    return m;
}

static SPRAT_Marker circle_marker(float x, float y, float radius)
{
    SPRAT_Marker m = point_marker(x, y);

    m.name = "circle";
    m.type = SPRAT_MARKERTYPE_CIRCLE;
    m.radius = radius;
    return m;
}

static SPRAT_Marker rect_marker(float x, float y, float w, float h)
{
    SPRAT_Marker m = point_marker(x, y);

    m.name = "rect";
    m.type = SPRAT_MARKERTYPE_RECTANGLE;
    m.w = w;
    m.h = h;
    return m;
}

static SPRAT_Marker polygon_marker(const SDL_FPoint *vertices, int num_vertices)
{
    SPRAT_Marker m = point_marker(vertices[0].x, vertices[0].y);

    m.name = "polygon";
    m.type = SPRAT_MARKERTYPE_POLYGON;
    m.vertices = vertices;
    m.num_vertices = num_vertices;
    return m;
}

/* A sprite of frame 0 of the overlap sheet */
static SPRAT_Sprite placed(float x, float y, float scale_x, float scale_y, double angle, SDL_FlipMode flip)
{
    SPRAT_Sprite t;

    t.sheet = &overlap_sheet;
    t.frame = 0;
    t.x = x;
    t.y = y;
    t.scale_x = scale_x;
    t.scale_y = scale_y;
    t.angle = angle;
    t.flip = flip;
    return t;
}

static SPRAT_Sprite at(float x, float y)
{
    return placed(x, y, 1.0f, 1.0f, 0.0, SDL_FLIP_NONE);
}

/* The sprite, showing another frame */
static SPRAT_Sprite on_frame(int frame, SPRAT_Sprite t)
{
    t.frame = frame;
    return t;
}

/* Check whether markers of two sprites overlap, in both orders */
#define CHECK_OVERLAP(marker_a, sprite_a, marker_b, sprite_b, expected) \
    check_overlap(__LINE__, (marker_a), (sprite_a), (marker_b), (sprite_b), (expected))

static void check_overlap(int line, SPRAT_Marker a, SPRAT_Sprite ta, SPRAT_Marker b, SPRAT_Sprite tb, bool expected)
{
    bool ab = SPRAT_MarkersOverlap(&ta, &a, &tb, &b);
    bool ba = SPRAT_MarkersOverlap(&tb, &b, &ta, &a);

    if (ab != expected || ba != expected) {
        fprintf(stderr, "%s:%d: %s and %s overlap: %d, the other way %d, expected %d\n",
                __FILE__, line, a.name, b.name, ab, ba, expected);
        failures++;
    }
}

static void test_markers_overlap(void)
{
    static const SDL_FPoint u_shape[] = {       /* a U, open at the top: the notch is x 3..7, y 0..7 */
        { 0, 0 }, { 3, 0 }, { 3, 7 }, { 7, 7 }, { 7, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 }
    };
    static const SDL_FPoint segment[] = { { -5, 5 }, { 15, 5 } };
    SPRAT_Marker square = rect_marker(0, 0, 10, 10);
    SPRAT_Marker small = rect_marker(0, 0, 2, 2);
    SPRAT_Marker origin = point_marker(0, 0);
    SPRAT_Marker circle = circle_marker(0, 0, 5);
    SPRAT_Marker unit = circle_marker(0, 0, 1);
    SPRAT_Marker u = polygon_marker(u_shape, COUNT(u_shape));
    SPRAT_Marker line = polygon_marker(segment, 2);
    SPRAT_Marker vertex = polygon_marker(segment, 1);
    SPRAT_Marker wide = rect_marker(-10, -1, 20, 2);
    SPRAT_Marker tall = rect_marker(-1, -10, 2, 20);
    SPRAT_Marker bar = rect_marker(2, 0, 3, 1);
    SPRAT_Marker centered = rect_marker(-1, -1, 2, 2);
    SPRAT_Marker strip = rect_marker(0, 0, 10, 2);
    SPRAT_Marker dot = circle_marker(0, 0, 0);
    SPRAT_Marker bad;
    SPRAT_Sprite t = at(0, 0), other;

    make_overlap_sheet();

    /* Rectangles: crossing, touching on an edge or a corner, apart, one inside the other */
    CHECK_OVERLAP(square, at(0, 0), square, at(5, 5), true);
    CHECK_OVERLAP(square, at(0, 0), square, at(10, 0), true);
    CHECK_OVERLAP(square, at(0, 0), square, at(10, 10), true);
    CHECK_OVERLAP(square, at(0, 0), square, at(10.5f, 0), false);
    CHECK_OVERLAP(square, at(0, 0), square, at(-10.5f, -3), false);
    CHECK_OVERLAP(square, at(0, 0), small, at(4, 4), true);
    CHECK_OVERLAP(wide, at(0, 0), tall, at(0, 0), true);       /* a cross: no corner inside the other */

    /* Points: in a shape, on its edge, outside, on each other */
    CHECK_OVERLAP(origin, at(5, 5), square, at(0, 0), true);
    CHECK_OVERLAP(origin, at(10, 5), square, at(0, 0), true);
    CHECK_OVERLAP(origin, at(11, 5), square, at(0, 0), false);
    CHECK_OVERLAP(origin, at(3, 4), origin, at(3, 4), true);
    CHECK_OVERLAP(origin, at(3, 4), origin, at(3, 4.5f), false);
    CHECK_OVERLAP(origin, at(3, 4), circle, at(0, 0), true);    /* on the circle: 3-4-5 */
    CHECK_OVERLAP(origin, at(3, 4.1f), circle, at(0, 0), false);

    /* Circles: crossing, touching, apart, inside */
    CHECK_OVERLAP(circle, at(0, 0), circle, at(10, 0), true);
    CHECK_OVERLAP(circle, at(0, 0), circle, at(6, 8), true);
    CHECK_OVERLAP(circle, at(0, 0), circle, at(6.1f, 8), false);
    CHECK_OVERLAP(circle, at(0, 0), unit, at(1, 1), true);

    /* A circle and a rectangle: along an edge, at a corner, each inside the other */
    CHECK_OVERLAP(circle, at(0, 0), square, at(5, -5), true);
    CHECK_OVERLAP(circle, at(0, 0), square, at(5.01f, -5), false);
    CHECK_OVERLAP(circle, at(0, 0), square, at(3, 4), true);   /* the corner on the circle */
    CHECK_OVERLAP(circle, at(0, 0), square, at(4, 4), false);  /* the corner 5.66 away */
    CHECK_OVERLAP(unit, at(5, 5), square, at(0, 0), true);
    CHECK_OVERLAP(circle, at(0, 0), small, at(-1, -1), true);

    /* A concave polygon: its notch is outside it */
    CHECK_OVERLAP(origin, at(5, 3), u, at(0, 0), false);
    CHECK_OVERLAP(origin, at(1, 3), u, at(0, 0), true);
    CHECK_OVERLAP(origin, at(5, 8), u, at(0, 0), true);
    CHECK_OVERLAP(unit, at(5, 3), u, at(0, 0), false);
    CHECK_OVERLAP(unit, at(5, 6), u, at(0, 0), true);          /* reaches the notch's floor at y 7 */
    CHECK_OVERLAP(small, at(4, 2), u, at(0, 0), false);
    CHECK_OVERLAP(small, at(6, 2), u, at(0, 0), true);          /* touches the notch's side at x 7 */

    /* Polygons of one and two vertices: a point and a segment */
    CHECK_OVERLAP(line, at(0, 0), square, at(0, 0), true);     /* both ends outside */
    CHECK_OVERLAP(line, at(0, 6), square, at(0, 0), false);
    CHECK_OVERLAP(vertex, at(5, 0), square, at(0, 0), true);   /* (0, 5) */
    CHECK_OVERLAP(vertex, at(-0.5f, 0), square, at(0, 0), false);

    /* Flipping mirrors around the pivot; a negative scale does too, and both undo each other */
    CHECK_OVERLAP(origin, at(-3, 0.5f), bar, at(0, 0), false);
    CHECK_OVERLAP(origin, at(-3, 0.5f), bar, placed(0, 0, 1, 1, 0.0, SDL_FLIP_HORIZONTAL), true);
    CHECK_OVERLAP(origin, at(-3, 0.5f), bar, placed(0, 0, -1, 1, 0.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(origin, at(-3, 0.5f), bar, placed(0, 0, -1, 1, 0.0, SDL_FLIP_HORIZONTAL), false);
    CHECK_OVERLAP(origin, at(3, -0.5f), bar, placed(0, 0, 1, 1, 0.0, SDL_FLIP_VERTICAL), true);

    /* Turning: clockwise around the pivot, exact at quarter turns */
    CHECK_OVERLAP(origin, at(-1, 5), strip, placed(0, 0, 1, 1, 90.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(origin, at(-2, 10), strip, placed(0, 0, 1, 1, 90.0, SDL_FLIP_NONE), true);  /* a corner */
    CHECK_OVERLAP(origin, at(5, 1), strip, placed(0, 0, 1, 1, 90.0, SDL_FLIP_NONE), false);
    CHECK_OVERLAP(origin, at(-5, -1), strip, placed(0, 0, 1, 1, -180.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(origin, at(1.3f, 0), centered, placed(0, 0, 1, 1, 45.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(origin, at(0.9f, 0.9f), centered, placed(0, 0, 1, 1, 45.0, SDL_FLIP_NONE), false);

    /* Scaling; a circle scaled unevenly is an ellipse, which turns with its frame */
    CHECK_OVERLAP(origin, at(6, 6), small, placed(0, 0, 3, 3, 0.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(origin, at(6.5f, 6), small, placed(0, 0, 3, 3, 0.0, SDL_FLIP_NONE), false);
    CHECK_OVERLAP(origin, at(3, 0.6f), unit, placed(0, 0, 4, 1, 0.0, SDL_FLIP_NONE), true);    /* 0.9225 */
    CHECK_OVERLAP(origin, at(3, 0.7f), unit, placed(0, 0, 4, 1, 0.0, SDL_FLIP_NONE), false);   /* 1.05 */
    CHECK_OVERLAP(origin, at(0.6f, 3), unit, placed(0, 0, 4, 1, 90.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(origin, at(3, 0.6f), unit, placed(0, 0, 4, 1, 90.0, SDL_FLIP_NONE), false);
    CHECK_OVERLAP(unit, placed(0, 0, 4, 1, 0.0, SDL_FLIP_NONE), unit, placed(4.5f, 0, 1, 4, 0.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(unit, placed(0, 0, 4, 1, 0.0, SDL_FLIP_NONE), unit, placed(5.1f, 0, 1, 4, 0.0, SDL_FLIP_NONE), false);
    CHECK_OVERLAP(unit, placed(0, 0, 4, 1, 90.0, SDL_FLIP_NONE), unit, at(1.9f, 0), true);
    CHECK_OVERLAP(unit, placed(0, 0, 4, 1, 90.0, SDL_FLIP_NONE), unit, at(2.1f, 0), false);
    CHECK_OVERLAP(unit, placed(0, 0, 4, 1, 90.0, SDL_FLIP_NONE), unit, at(0, 4.9f), true);
    CHECK_OVERLAP(unit, placed(0, 0, 2, 2, 0.0, SDL_FLIP_NONE), unit, placed(3, 0, 1, 1, 0.0, SDL_FLIP_NONE), true);  /* touching */

    /* Scaled to 0: flattened to a segment or a point, which still overlaps what it touches */
    CHECK_OVERLAP(origin, at(0, 1), centered, placed(0, 0, 0, 1, 0.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(origin, at(0.1f, 0), centered, placed(0, 0, 0, 1, 0.0, SDL_FLIP_NONE), false);
    CHECK_OVERLAP(origin, at(0, 1.9f), circle_marker(0, 0, 2), placed(0, 0, 0, 1, 0.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(origin, at(0, 2.1f), circle_marker(0, 0, 2), placed(0, 0, 0, 1, 0.0, SDL_FLIP_NONE), false);
    CHECK_OVERLAP(origin, at(1.9f, 0), circle_marker(0, 0, 2), placed(0, 0, 1, 0, 0.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(unit, placed(5, 5, 0, 0, 0.0, SDL_FLIP_NONE), square, at(0, 0), true);
    CHECK_OVERLAP(unit, placed(5, 5, 0, 0, 0.0, SDL_FLIP_NONE), unit, placed(5, 5, 0, 0, 0.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(unit, placed(0, 0, 0, 1, 0.0, SDL_FLIP_NONE), unit, placed(0.5f, 0, 1, 0, 0.0, SDL_FLIP_NONE), true);
    CHECK_OVERLAP(dot, at(3, 4), circle, at(0, 0), true);      /* a radius of 0 is a point */
    CHECK_OVERLAP(dot, at(3, 4.1f), circle, at(0, 0), false);

    /* Each marker is placed by its own frame's pivot: frame 1's (3, -2) */
    CHECK_OVERLAP(origin, on_frame(1, at(0, 0)), origin, at(-3, 2), true);
    CHECK_OVERLAP(origin, on_frame(1, at(0, 0)), origin, on_frame(1, at(0, 0)), true);
    CHECK_OVERLAP(origin, on_frame(1, at(0, 0)), origin, at(0, 0), false);
    CHECK_OVERLAP(small, on_frame(1, placed(0, 0, 1, 1, 0.0, SDL_FLIP_HORIZONTAL)), origin, at(2, 3), true);   /* x 1..3, y 2..4 */
    CHECK_OVERLAP(small, on_frame(1, placed(0, 0, 1, 1, 0.0, SDL_FLIP_HORIZONTAL)), origin, at(4, 3), false);

    /* Errors */
    CHECK(!SPRAT_MarkersOverlap(NULL, &origin, &t, &origin));
    CHECK_ERROR("sprite_a");
    CHECK(!SPRAT_MarkersOverlap(&t, &origin, NULL, &origin));
    CHECK_ERROR("sprite_b");
    other = t;
    other.sheet = NULL;
    CHECK(!SPRAT_MarkersOverlap(&t, &origin, &other, &origin));
    CHECK_ERROR("sheet");
    other = t;
    other.frame = 2;
    CHECK(!SPRAT_MarkersOverlap(&t, &origin, &other, &origin));
    CHECK_ERROR("Frame 2 is out of range");
    other.frame = -1;
    CHECK(!SPRAT_MarkersOverlap(&other, &origin, &t, &origin));
    CHECK_ERROR("Frame -1 is out of range");
    CHECK(!SPRAT_MarkersOverlap(&t, NULL, &t, &origin));
    CHECK_ERROR("marker_a");
    CHECK(!SPRAT_MarkersOverlap(&t, &origin, &t, NULL));
    CHECK_ERROR("marker_b");
    bad = polygon_marker(u_shape, 0);
    CHECK(!SPRAT_MarkersOverlap(&t, &origin, &t, &bad));
    CHECK_ERROR("Polygon marker polygon has no vertices");
    bad = point_marker(0, 0);
    bad.type = (SPRAT_MarkerType)9;
    CHECK(!SPRAT_MarkersOverlap(&t, &bad, &t, &origin));
    CHECK_ERROR("Marker point has an unknown type 9");
}

/* A reference for the overlap test, written apart from the library: points of one marker,
   sampled densely, are looked up in the other through the inverse of its frame's placement */

#define SAMPLE_STEP 0.25    /* between samples, in original-image pixels */

typedef struct ref_point
{
    double x, y;
} ref_point;

typedef struct placed_marker
{
    const SPRAT_Marker *marker;
    SPRAT_Sprite sprite;
} placed_marker;

static void get_mirrored_scale(const SPRAT_Sprite *t, double *sx, double *sy)
{
    *sx = (t->flip & SDL_FLIP_HORIZONTAL) ? -t->scale_x : t->scale_x;
    *sy = (t->flip & SDL_FLIP_VERTICAL) ? -t->scale_y : t->scale_y;
}

/* The pivot of a placed marker's frame */
static const SDL_FPoint *get_pivot(const placed_marker *p)
{
    return &p->sprite.sheet->frames[p->sprite.frame].pivot;
}

static ref_point to_screen(const placed_marker *p, double x, double y)
{
    const SDL_FPoint *pivot = get_pivot(p);
    double radians = p->sprite.angle * SDL_PI_D / 180.0;
    double sx, sy, dx, dy;
    ref_point r;

    get_mirrored_scale(&p->sprite, &sx, &sy);
    dx = (x - pivot->x) * sx;
    dy = (y - pivot->y) * sy;
    r.x = p->sprite.x + dx * SDL_cos(radians) - dy * SDL_sin(radians);
    r.y = p->sprite.y + dx * SDL_sin(radians) + dy * SDL_cos(radians);
    return r;
}

static ref_point to_original(const placed_marker *p, ref_point s)
{
    const SDL_FPoint *pivot = get_pivot(p);
    double radians = p->sprite.angle * SDL_PI_D / 180.0;
    double dx = s.x - p->sprite.x, dy = s.y - p->sprite.y;
    double sx, sy;
    ref_point r;

    get_mirrored_scale(&p->sprite, &sx, &sy);
    r.x = (dx * SDL_cos(radians) + dy * SDL_sin(radians)) / sx + pivot->x;
    r.y = (-dx * SDL_sin(radians) + dy * SDL_cos(radians)) / sy + pivot->y;
    return r;
}

static double distance_to_segment(ref_point p, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay;
    double length = dx * dx + dy * dy;
    double t = (length > 0.0) ? SDL_clamp(((p.x - ax) * dx + (p.y - ay) * dy) / length, 0.0, 1.0) : 0.0;
    double ex = ax + t * dx - p.x, ey = ay + t * dy - p.y;

    return SDL_sqrt(ex * ex + ey * ey);
}

/* How far an original-image point is inside a marker: positive inside, negative outside */
static double depth_in_marker(const SPRAT_Marker *m, ref_point p)
{
    double x0 = m->position.x, y0 = m->position.y;
    int i;

    switch (m->type) {
    case SPRAT_MARKERTYPE_CIRCLE:
        return m->radius - SDL_sqrt((p.x - x0) * (p.x - x0) + (p.y - y0) * (p.y - y0));
    case SPRAT_MARKERTYPE_RECTANGLE: {
        double inside = SDL_min(SDL_min(p.x - x0, x0 + m->w - p.x), SDL_min(p.y - y0, y0 + m->h - p.y));
        double ox = SDL_max(SDL_max(x0 - p.x, p.x - (x0 + m->w)), 0.0);
        double oy = SDL_max(SDL_max(y0 - p.y, p.y - (y0 + m->h)), 0.0);
        return (inside >= 0.0) ? inside : -SDL_sqrt(ox * ox + oy * oy);
    }
    case SPRAT_MARKERTYPE_POLYGON: {
        double nearest = SDL_MAX_SINT32;
        bool inside = false;

        for (i = 0; i < m->num_vertices; i++) {
            const SDL_FPoint *a = &m->vertices[i];
            const SDL_FPoint *b = &m->vertices[(i + 1) % m->num_vertices];

            nearest = SDL_min(nearest, distance_to_segment(p, a->x, a->y, b->x, b->y));
            if (m->num_vertices >= 3 && (a->y > p.y) != (b->y > p.y) &&
                p.x < (double)(b->x - a->x) * (p.y - a->y) / (double)(b->y - a->y) + a->x) {
                inside = !inside;
            }
        }
        return inside ? nearest : -nearest;
    }
    default:
        return -SDL_sqrt((p.x - x0) * (p.x - x0) + (p.y - y0) * (p.y - y0));
    }
}

/* Sample a marker's points, in original-image pixels: its outline and its inside */
static int sample_marker(const SPRAT_Marker *m, ref_point *samples, int capacity)
{
    double x0, y0, x1, y1, x, y;
    int count = 0, i, steps, k;

#define ADD_SAMPLE(px, py) do { if (count < capacity) { samples[count].x = (px); samples[count].y = (py); count++; } } while (0)

    switch (m->type) {
    case SPRAT_MARKERTYPE_POINT:
        ADD_SAMPLE(m->position.x, m->position.y);
        return count;
    case SPRAT_MARKERTYPE_CIRCLE:
        steps = (int)SDL_ceil(2.0 * SDL_PI_D * m->radius / SAMPLE_STEP) + 1;
        for (k = 0; k < steps; k++) {
            ADD_SAMPLE(m->position.x + m->radius * SDL_cos(2.0 * SDL_PI_D * k / steps),
                       m->position.y + m->radius * SDL_sin(2.0 * SDL_PI_D * k / steps));
        }
        x0 = m->position.x - m->radius;
        y0 = m->position.y - m->radius;
        x1 = m->position.x + m->radius;
        y1 = m->position.y + m->radius;
        break;
    case SPRAT_MARKERTYPE_RECTANGLE:
        x0 = m->position.x;
        y0 = m->position.y;
        x1 = x0 + m->w;
        y1 = y0 + m->h;
        break;
    default:
        x0 = x1 = m->vertices[0].x;
        y0 = y1 = m->vertices[0].y;
        for (i = 0; i < m->num_vertices; i++) {
            const SDL_FPoint *a = &m->vertices[i];
            const SDL_FPoint *b = &m->vertices[(i + 1) % m->num_vertices];
            double length = SDL_sqrt((double)(b->x - a->x) * (b->x - a->x) + (double)(b->y - a->y) * (b->y - a->y));

            steps = (int)SDL_ceil(length / SAMPLE_STEP) + 1;
            for (k = 0; k <= steps; k++) {
                ADD_SAMPLE(a->x + (b->x - a->x) * (double)k / steps, a->y + (b->y - a->y) * (double)k / steps);
            }
            x0 = SDL_min(x0, a->x);
            y0 = SDL_min(y0, a->y);
            x1 = SDL_max(x1, a->x);
            y1 = SDL_max(y1, a->y);
        }
        break;
    }

    /* The inside, and for a rectangle its outline too, on a grid that ends on its far edges */
    for (y = y0;; y = SDL_min(y + SAMPLE_STEP, y1)) {
        for (x = x0;; x = SDL_min(x + SAMPLE_STEP, x1)) {
            ref_point p;

            p.x = x;
            p.y = y;
            if (depth_in_marker(m, p) >= 0.0) {
                ADD_SAMPLE(x, y);
            }
            if (x >= x1) {
                break;
            }
        }
        if (y >= y1) {
            break;
        }
    }
#undef ADD_SAMPLE
    return count;
}

static SPRAT_Marker random_marker(SDL_FPoint vertices[8])
{
    float x = SDL_randf() * 8.0f - 4.0f, y = SDL_randf() * 8.0f - 4.0f;
    int kind = SDL_rand(6);
    int i, count;

    switch (kind) {
    case 0:
        return point_marker(x, y);
    case 1:
        return circle_marker(x, y, 0.5f + SDL_randf() * 3.5f);
    case 2:
        return rect_marker(x, y, 0.5f + SDL_randf() * 5.5f, 0.5f + SDL_randf() * 5.5f);
    case 3:
        /* A segment */
        vertices[0].x = x;
        vertices[0].y = y;
        vertices[1].x = x + SDL_randf() * 8.0f - 4.0f;
        vertices[1].y = y + SDL_randf() * 8.0f - 4.0f;
        return polygon_marker(vertices, 2);
    default:
        /* Star-shaped around (x, y), so simple, and often concave */
        count = 3 + SDL_rand(6);
        for (i = 0; i < count; i++) {
            double angle = 2.0 * SDL_PI_D * (i + 0.2 + 0.6 * SDL_randf()) / count;
            float radius = 0.5f + SDL_randf() * 4.0f;

            vertices[i].x = x + radius * (float)SDL_cos(angle);
            vertices[i].y = y + radius * (float)SDL_sin(angle);
        }
        return polygon_marker(vertices, count);
    }
}

static SPRAT_Sprite random_sprite(int frame, float spread)
{
    static const float scales[] = { 1.0f, 2.0f, 0.5f, -1.0f, 1.5f, 0.75f };
    static const double angles[] = { 0.0, 90.0, 180.0, 270.0 };

    return on_frame(frame, placed(SDL_randf() * 2.0f * spread - spread, SDL_randf() * 2.0f * spread - spread,
                                  scales[SDL_rand(COUNT(scales))], scales[SDL_rand(COUNT(scales))],
                                  SDL_rand(2) ? angles[SDL_rand(COUNT(angles))] : SDL_randf() * 360.0,
                                  (SDL_FlipMode)SDL_rand(4)));
}

/* Random markers on random placements: when the function says they overlap, a sample of one is
   within the sampling distance of the other; when it says they don't, no sample is inside it */
static void test_markers_overlap_matches_reference(void)
{
    enum { CASES = 3000, CAPACITY = 4096 };
    static ref_point samples[CAPACITY];
    int disagreements = 0, overlaps = 0;
    int i, s;

    make_overlap_sheet();
    SDL_srand(20261026);

    for (i = 0; i < CASES; i++) {
        SDL_FPoint vertices_a[8], vertices_b[8];
        SPRAT_Marker ma = random_marker(vertices_a);
        SPRAT_Marker mb = random_marker(vertices_b);
        placed_marker a, b;
        double sxa, sya, sxb, syb, reach, tolerance;
        bool overlap, near = false, inside = false;
        int count, fa = SDL_rand(2), fb = SDL_rand(2);

        if (mb.type == SPRAT_MARKERTYPE_POINT) {
            continue;   /* a sample never lands exactly on a point */
        }

        a.marker = &ma;
        a.sprite = random_sprite(fa, 1.0f);
        b.marker = &mb;
        b.sprite = random_sprite(fb, 5.0f);

        overlap = SPRAT_MarkersOverlap(&a.sprite, &ma, &b.sprite, &mb);
        if (overlap != SPRAT_MarkersOverlap(&b.sprite, &mb, &a.sprite, &ma)) {
            fprintf(stderr, "overlap case %d: the result depends on the order\n", i);
            failures++;
        }
        overlaps += overlap;

        /* Neighbouring samples of a are reach apart on the screen, which is at most tolerance in
           b's original image */
        get_mirrored_scale(&a.sprite, &sxa, &sya);
        get_mirrored_scale(&b.sprite, &sxb, &syb);
        reach = SAMPLE_STEP * SDL_max(SDL_fabs(sxa), SDL_fabs(sya)) * 1.5;
        tolerance = reach / SDL_min(SDL_fabs(sxb), SDL_fabs(syb));

        count = sample_marker(&ma, samples, CAPACITY);
        CHECK(count < CAPACITY);
        for (s = 0; s < count; s++) {
            double depth = depth_in_marker(&mb, to_original(&b, to_screen(&a, samples[s].x, samples[s].y)));

            near = near || depth >= -tolerance;
            inside = inside || depth > 1e-6;
        }

        if ((overlap && !near) || (!overlap && inside)) {
            if (disagreements++ < 10) {
                fprintf(stderr, "overlap case %d: %s on frame %d at %g,%g scale %gx%g turn %g flip %d, %s on frame %d at %g,%g scale %gx%g turn %g flip %d: "
                        "overlap is %d, the reference finds a sample %s\n", i,
                        ma.name, fa, a.sprite.x, a.sprite.y, a.sprite.scale_x, a.sprite.scale_y, a.sprite.angle, a.sprite.flip,
                        mb.name, fb, b.sprite.x, b.sprite.y, b.sprite.scale_x, b.sprite.scale_y, b.sprite.angle, b.sprite.flip,
                        overlap, overlap ? "nowhere near" : "inside");
            }
        }
    }

    CHECK_INT(disagreements, 0);
    /* Both answers are common, so both directions are checked */
    CHECK(overlaps > CASES / 10);
    CHECK(overlaps < CASES * 9 / 10);
}

/* The distance from the origin to segment a-b */
static double distance_to_origin(ref_point a, ref_point b)
{
    ref_point origin = { 0.0, 0.0 };

    return distance_to_segment(origin, a.x, a.y, b.x, b.y);
}

/* An unevenly scaled, turned circle grown until it touches another marker: just short of that,
   the markers don't overlap, and just past it they do. The ellipse is the unit circle scaled by
   k * (ax, ay), so in its unit space at k = 1 the other marker is d away from the center, and
   they touch at k = d. For an ellipse, d is found on many points of its outline */
static void test_markers_overlap_at_threshold(void)
{
    enum { CASES = 300, OUTLINE = 20000 };
    SPRAT_Marker unit = circle_marker(0, 0, 1);
    int checked = 0, i, k;

    make_overlap_sheet();
    SDL_srand(1013);

    for (i = 0; i < CASES; i++) {
        SDL_FPoint vertices[8];
        SPRAT_Marker mb = random_marker(vertices);
        placed_marker a, b;
        SPRAT_Sprite ta;
        double ax = 0.3 + SDL_randf() * 3.0, ay = 0.3 + SDL_randf() * 3.0;
        double d = SDL_MAX_SINT32;

        if (mb.type == SPRAT_MARKERTYPE_POINT) {
            continue;
        }

        a.marker = &unit;
        a.sprite = placed(0, 0, (float)ax, (float)ay, SDL_rand(2) ? 90.0 : SDL_randf() * 360.0, (SDL_FlipMode)SDL_rand(4));
        b.marker = &mb;
        b.sprite = random_sprite(SDL_rand(2), 6.0f);

        /* The ellipse's center must be outside the other marker, clearly */
        {
            ref_point center = { 0.0, 0.0 };
            if (depth_in_marker(&mb, to_original(&b, center)) > -0.05) {
                continue;
            }
        }

        if (mb.type == SPRAT_MARKERTYPE_CIRCLE) {
            for (k = 0; k < OUTLINE; k++) {
                double t = 2.0 * SDL_PI_D * k / (double)OUTLINE;
                ref_point p = to_original(&a, to_screen(&b, mb.position.x + mb.radius * SDL_cos(t), mb.position.y + mb.radius * SDL_sin(t)));
                d = SDL_min(d, SDL_sqrt(p.x * p.x + p.y * p.y));
            }
        } else {
            SDL_FPoint corners[4];
            const SDL_FPoint *v = mb.vertices;
            int n = mb.num_vertices;

            if (mb.type == SPRAT_MARKERTYPE_RECTANGLE) {
                corners[0] = mb.position;
                corners[1].x = mb.position.x + mb.w;
                corners[1].y = mb.position.y;
                corners[2].x = mb.position.x + mb.w;
                corners[2].y = mb.position.y + mb.h;
                corners[3].x = mb.position.x;
                corners[3].y = mb.position.y + mb.h;
                v = corners;
                n = 4;
            }
            for (k = 0; k < n; k++) {
                ref_point p0 = to_original(&a, to_screen(&b, v[k].x, v[k].y));
                ref_point p1 = to_original(&a, to_screen(&b, v[(k + 1) % n].x, v[(k + 1) % n].y));
                d = SDL_min(d, distance_to_origin(p0, p1));
            }
        }

        ta = a.sprite;
        ta.scale_x = (float)(ax * d * 0.999);
        ta.scale_y = (float)(ay * d * 0.999);
        if (SPRAT_MarkersOverlap(&ta, &unit, &b.sprite, &mb) || SPRAT_MarkersOverlap(&b.sprite, &mb, &ta, &unit)) {
            fprintf(stderr, "threshold case %d: the ellipse overlaps %s 0.1%% short of touching it\n", i, mb.name);
            failures++;
        }
        ta.scale_x = (float)(ax * d * 1.001);
        ta.scale_y = (float)(ay * d * 1.001);
        if (!SPRAT_MarkersOverlap(&ta, &unit, &b.sprite, &mb) || !SPRAT_MarkersOverlap(&b.sprite, &mb, &ta, &unit)) {
            fprintf(stderr, "threshold case %d: the ellipse doesn't overlap %s 0.1%% past touching it\n", i, mb.name);
            failures++;
        }
        checked++;
    }
    CHECK(checked > CASES / 2);
}

#define CHECK_CENTER(marker, sprite, ex, ey) \
    check_center(__LINE__, (marker), (sprite), (ex), (ey))

static void check_center(int line, SPRAT_Marker marker, SPRAT_Sprite t, float ex, float ey)
{
    SDL_FPoint p = { -1234.0f, -1234.0f };

    if (!SPRAT_GetMarkerCenter(&t, &marker, &p) ||
        SDL_fabsf(p.x - ex) > 1e-4f || SDL_fabsf(p.y - ey) > 1e-4f) {
        fprintf(stderr, "%s:%d: %s's center is %g,%g, expected %g,%g (%s)\n", __FILE__, line, marker.name, p.x, p.y, ex, ey, SDL_GetError());
        failures++;
    }
}

static void test_marker_center(void)
{
    static const SDL_FPoint triangle[] = { { 0, 0 }, { 6, 0 }, { 0, 3 } };
    static const SDL_FPoint square_5[] = { { 0, 0 }, { 2, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 } };  /* an extra vertex on the top edge */
    static const SDL_FPoint l_shape[] = { { 0, 0 }, { 4, 0 }, { 4, 1 }, { 1, 1 }, { 1, 4 }, { 0, 4 } };
    static const SDL_FPoint clockwise[] = { { 0, 0 }, { 0, 4 }, { 4, 4 }, { 4, 0 } };
    static const SDL_FPoint segment[] = { { 1, 1 }, { 5, 3 } };
    static const SDL_FPoint collinear[] = { { 2, 2 }, { 0, 0 }, { 6, 6 }, { 3, 3 } };
    static const SDL_FPoint lone[] = { { 7, -2 } };
    SPRAT_Marker rect = rect_marker(2, 4, 6, 10);
    SPRAT_Marker bad;
    SPRAT_Sprite t = at(0, 0);
    SDL_FPoint p;
    int i;

    make_overlap_sheet();

    /* Each type, unplaced */
    CHECK_CENTER(point_marker(3, 4), at(0, 0), 3, 4);
    CHECK_CENTER(circle_marker(3, 4, 9), at(0, 0), 3, 4);
    CHECK_CENTER(rect, at(0, 0), 5, 9);
    CHECK_CENTER(polygon_marker(triangle, 3), at(0, 0), 2, 1);
    CHECK_CENTER(polygon_marker(clockwise, 4), at(0, 0), 2, 2);   /* either winding */

    /* A polygon's centroid is its center of mass: not moved by an extra vertex, and outside an L */
    CHECK_CENTER(polygon_marker(square_5, 5), at(0, 0), 2, 2);
    CHECK_CENTER(polygon_marker(l_shape, 6), at(0, 0), 19.0f / 14.0f, 19.0f / 14.0f);

    /* Without area: a segment's middle, a line's ends' middle, a lone vertex */
    CHECK_CENTER(polygon_marker(segment, 2), at(0, 0), 3, 2);
    CHECK_CENTER(polygon_marker(collinear, 4), at(0, 0), 3, 3);
    CHECK_CENTER(polygon_marker(lone, 1), at(0, 0), 7, -2);

    /* Placed: moved, by the frame's pivot, flipped and scaled around it, turned clockwise */
    CHECK_CENTER(rect, at(100, 50), 105, 59);
    CHECK_CENTER(rect, on_frame(1, at(100, 50)), 102, 61);                   /* (5, 9) - pivot (3, -2) */
    CHECK_CENTER(rect, placed(100, 50, 1, 1, 0.0, SDL_FLIP_HORIZONTAL), 95, 59);
    CHECK_CENTER(rect, placed(100, 50, 1, 1, 0.0, SDL_FLIP_VERTICAL), 105, 41);
    CHECK_CENTER(rect, placed(100, 50, -1, 1, 0.0, SDL_FLIP_NONE), 95, 59);
    CHECK_CENTER(rect, placed(100, 50, 2, 3, 0.0, SDL_FLIP_NONE), 110, 77);
    CHECK_CENTER(rect, placed(100, 50, 1, 1, 90.0, SDL_FLIP_NONE), 91, 55);
    CHECK_CENTER(rect, placed(100, 50, 1, 1, -90.0, SDL_FLIP_NONE), 109, 45);
    CHECK_CENTER(rect, placed(100, 50, 0, 0, 33.0, SDL_FLIP_NONE), 100, 50);  /* scaled to 0: on the pivot */
    CHECK_CENTER(polygon_marker(l_shape, 6), on_frame(1, placed(10, 20, 2, 2, 180.0, SDL_FLIP_HORIZONTAL)),
                 10 + 2 * (19.0f / 14.0f - 3), 20 - 2 * (19.0f / 14.0f + 2));

    /* Wherever it is placed, it is where SPRAT_GetSpritePoint() puts the unplaced center, and a
       circle's center is inside the circle */
    SDL_srand(77);
    for (i = 0; i < 200; i++) {
        SDL_FPoint vertices[8], center, expected;
        SPRAT_Marker m = random_marker(vertices);
        int frame = SDL_rand(2);
        SPRAT_Sprite r = random_sprite(frame, 50.0f);

        t.frame = frame;
        CHECK(SPRAT_GetMarkerCenter(&t, &m, &center));
        if (frame == 1) {   /* at(0, 0) on frame 1 moves it by the pivot */
            center.x += 3.0f;
            center.y -= 2.0f;
        }
        CHECK(SPRAT_GetSpritePoint(&r, &center, &expected));
        CHECK(SPRAT_GetMarkerCenter(&r, &m, &p));
        if (SDL_fabsf(p.x - expected.x) > 1e-3f || SDL_fabsf(p.y - expected.y) > 1e-3f) {
            fprintf(stderr, "center case %d: %s placed at %g,%g is %g,%g, SPRAT_GetSpritePoint() says %g,%g\n",
                    i, m.name, r.x, r.y, p.x, p.y, expected.x, expected.y);
            failures++;
        }
        if (m.type == SPRAT_MARKERTYPE_CIRCLE) {
            SPRAT_Marker dot = point_marker(p.x, p.y);
            SPRAT_Sprite screen = at(0, 0);     /* frame 0's pivot is its origin */
            CHECK(SPRAT_MarkersOverlap(&r, &m, &screen, &dot));
        }
    }

    /* Errors */
    t = at(0, 0);
    CHECK(!SPRAT_GetMarkerCenter(NULL, &rect, &p));
    CHECK_ERROR("sprite");
    t.sheet = NULL;
    CHECK(!SPRAT_GetMarkerCenter(&t, &rect, &p));
    CHECK_ERROR("sheet");
    t = at(0, 0);
    t.frame = 2;
    CHECK(!SPRAT_GetMarkerCenter(&t, &rect, &p));
    CHECK_ERROR("Frame 2 is out of range");
    t.frame = 0;
    CHECK(!SPRAT_GetMarkerCenter(&t, NULL, &p));
    CHECK_ERROR("marker");
    CHECK(!SPRAT_GetMarkerCenter(&t, &rect, NULL));
    CHECK_ERROR("result");
    bad = polygon_marker(triangle, 0);
    CHECK(!SPRAT_GetMarkerCenter(&t, &bad, &p));
    CHECK_ERROR("Polygon marker polygon has no vertices");
    bad = point_marker(0, 0);
    bad.type = (SPRAT_MarkerType)9;
    CHECK(!SPRAT_GetMarkerCenter(&t, &bad, &p));
    CHECK_ERROR("Marker point has an unknown type 9");
}

static void test_marker_outline(void)
{
    static const SDL_FPoint pentagon[] = { { 6, 0 }, { 10, 6 }, { 10, 40 }, { 3, 40 }, { 3, 6 } };
    static const SDL_FPoint segment[] = { { 1, 1 }, { 5, 3 } };
    static const SDL_FPoint lone[] = { { 7, -2 } };
    SDL_FPoint points[300], expected;
    SPRAT_Marker rect = rect_marker(2, 4, 6, 10);
    SPRAT_Marker polygon = polygon_marker(pentagon, COUNT(pentagon));
    SPRAT_Marker circle = circle_marker(3, 4, 5);
    SPRAT_Marker bad;
    SPRAT_Sprite t = at(0, 0);
    int count, previous, i, k;

    make_overlap_sheet();

    /* A point is its point; a rectangle, its corners from the top-left and back */
    t = placed(100, 50, 2, 3, 90.0, SDL_FLIP_HORIZONTAL);
    t.frame = 1;
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &(SPRAT_Marker){ 0 }, points, COUNT(points)), 1);
    count = SPRAT_GetMarkerOutline(&t, &rect, points, COUNT(points));
    CHECK_INT(count, 5);
    for (i = 0; i < 5; i++) {
        SDL_FPoint corner;

        corner.x = rect.position.x + ((i == 1 || i == 2) ? rect.w : 0.0f);
        corner.y = rect.position.y + ((i == 2 || i == 3) ? rect.h : 0.0f);
        CHECK(SPRAT_GetSpritePoint(&t, &corner, &expected));
        CHECK_FLOAT(points[i].x, expected.x);
        CHECK_FLOAT(points[i].y, expected.y);
    }

    /* A polygon: its vertices and back to the first; a segment back and forth; a lone vertex */
    t.frame = 0;
    count = SPRAT_GetMarkerOutline(&t, &polygon, points, COUNT(points));
    CHECK_INT(count, 6);
    for (i = 0; i < 6; i++) {
        CHECK(SPRAT_GetSpritePoint(&t, &pentagon[i % 5], &expected));
        CHECK_FLOAT(points[i].x, expected.x);
        CHECK_FLOAT(points[i].y, expected.y);
    }
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &(SPRAT_Marker){ "s", SPRAT_MARKERTYPE_POLYGON, { 1, 1 }, 0, 0, 0, segment, 2 }, points, COUNT(points)), 3);
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &(SPRAT_Marker){ "l", SPRAT_MARKERTYPE_POLYGON, { 7, -2 }, 0, 0, 0, lone, 1 }, points, COUNT(points)), 1);

    /* A circle: points on the ellipse it is placed as, closed, more of them as it grows, and
       each side within a quarter of a pixel of the curve */
    previous = 0;
    for (k = 0; k < 6; k++) {
        static const float scales[][2] = { { 0.01f, 0.01f }, { 1, 1 }, { 4, 1 }, { 1, 4 }, { 10, 10 }, { 1000, 1000 } };
        placed_marker reference;

        t = placed(40, 30, scales[k][0], scales[k][1], 33.0, SDL_FLIP_VERTICAL);
        t.frame = 1;
        count = SPRAT_GetMarkerOutline(&t, &circle, points, COUNT(points));
        CHECK(count >= 9 && count <= 257);
        CHECK(count >= previous || k == 3);     /* (1, 4) is (4, 1) turned the other way */
        previous = count;
        CHECK_FLOAT(points[count - 1].x, points[0].x);
        CHECK_FLOAT(points[count - 1].y, points[0].y);

        reference.marker = &circle;
        reference.sprite = t;
        for (i = 0; i < count; i++) {
            ref_point p = { points[i].x, points[i].y };
            ref_point o = to_original(&reference, p);
            double distance = SDL_sqrt((o.x - 3) * (o.x - 3) + (o.y - 4) * (o.y - 4));

            if (SDL_fabs(distance - 5.0) > 5e-3 * 5.0) {
                fprintf(stderr, "outline point %d of a circle scaled %gx%g is %g from its center\n", i, t.scale_x, t.scale_y, distance);
                failures++;
                break;
            }
        }
        if (scales[k][0] == scales[k][1] && count < 257) {
            /* Evenly scaled: a side's middle is inside the circle by at most the tolerance */
            double r = 5.0 * scales[k][0];
            ref_point c = to_screen(&reference, 3, 4);

            for (i = 0; i + 1 < count; i++) {
                double mx = (points[i].x + points[i + 1].x) / 2.0 - c.x, my = (points[i].y + points[i + 1].y) / 2.0 - c.y;
                CHECK(r - SDL_sqrt(mx * mx + my * my) <= 0.25 + 1e-3);
            }
        }
    }
    CHECK_INT(previous, 257);   /* the largest, of a radius of 5000 pixels, is capped */

    /* Like snprintf(): the count, and only what fits written */
    t = at(0, 0);
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &rect, NULL, 0), 5);
    for (i = 0; i < 5; i++) {
        points[i].x = points[i].y = -1.0f;
    }
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &rect, points, 2), 5);
    CHECK_FLOAT(points[1].x, 8);
    CHECK_FLOAT(points[2].x, -1);
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &circle, NULL, 0),
              SPRAT_GetMarkerOutline(&t, &circle, points, COUNT(points)));

    /* Errors */
    CHECK_INT(SPRAT_GetMarkerOutline(NULL, &rect, points, 5), -1);
    CHECK_ERROR("sprite");
    t.sheet = NULL;
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &rect, points, 5), -1);
    CHECK_ERROR("sheet");
    t = at(0, 0);
    t.frame = 2;
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &rect, points, 5), -1);
    CHECK_ERROR("Frame 2 is out of range");
    t.frame = 0;
    CHECK_INT(SPRAT_GetMarkerOutline(&t, NULL, points, 5), -1);
    CHECK_ERROR("marker");
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &rect, NULL, 5), -1);
    CHECK_ERROR("points");
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &rect, points, -1), -1);
    CHECK_ERROR("points");
    bad = polygon_marker(pentagon, 0);
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &bad, points, 5), -1);
    CHECK_ERROR("Polygon marker polygon has no vertices");
    bad = point_marker(0, 0);
    bad.type = (SPRAT_MarkerType)9;
    CHECK_INT(SPRAT_GetMarkerOutline(&t, &bad, points, 5), -1);
    CHECK_ERROR("Marker point has an unknown type 9");
}

/* ---- Rendering ------------------------------------------------------------ */

#define ATLAS_SIZE 64
#define SCREEN 160

/* Renders go to a target texture of a hardware renderer, on an offscreen window, so they keep
   their subpixel precision. Without one, the software renderer is used: it snaps fractional
   coordinates to whole pixels, so only the cases whose geometry is whole are checked */
static SDL_Window *window = NULL;
static SDL_Surface *software_surface = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *target = NULL;
static bool hardware = false;
static int skipped = 0;
static Uint32 screen[SCREEN][SCREEN];

static bool create_renderer(void)
{
    /* The environment's SDL_VIDEO_DRIVER and SDL_RENDER_DRIVER take precedence */
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen,dummy");

    if (SDL_Init(SDL_INIT_VIDEO)) {
        window = SDL_CreateWindow("test_sprat_sdl", SCREEN, SCREEN, SDL_WINDOW_HIDDEN);
        if (window) {
            renderer = SDL_CreateRenderer(window, NULL);
        }
    }
    if (!renderer) {
        software_surface = SDL_CreateSurface(SCREEN, SCREEN, SDL_PIXELFORMAT_RGBA8888);
        renderer = SDL_CreateSoftwareRenderer(software_surface);
    }
    if (!renderer) {
        fprintf(stderr, "Couldn't create a renderer: %s\n", SDL_GetError());
        return false;
    }

    hardware = (SDL_strcmp(SDL_GetRendererName(renderer), SDL_SOFTWARE_RENDERER) != 0);
    printf("rendering with %s on %s\n", SDL_GetRendererName(renderer),
           window ? SDL_GetCurrentVideoDriver() : "a surface");

    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SCREEN, SCREEN);
    if (!target || !SDL_SetRenderTarget(renderer, target)) {
        fprintf(stderr, "Couldn't create a render target: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_NONE);
    return true;
}

static void destroy_renderer(void)
{
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_DestroySurface(software_surface);
}

static void clear(void)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
}

/* Read the render target into screen, as RGBA8888 values */
static void read_screen(void)
{
    SDL_Surface *read = SDL_RenderReadPixels(renderer, NULL);
    SDL_Surface *converted = read ? SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA8888) : NULL;
    int y;

    SDL_zeroa(screen);
    if (!converted) {
        fprintf(stderr, "Couldn't read the render target: %s\n", SDL_GetError());
        failures++;
    } else {
        for (y = 0; y < SCREEN && y < converted->h; y++) {
            SDL_memcpy(screen[y], (const Uint8 *)converted->pixels + y * converted->pitch, (size_t)SDL_min(SCREEN, converted->w) * 4);
        }
    }
    SDL_DestroySurface(converted);
    SDL_DestroySurface(read);
}

static bool is_whole(double value)
{
    return value == SDL_floor(value);
}

/* Whether the software renderer draws a call exactly: in quarter turns, with the corners on
   whole pixels */
static bool is_exact(const DrawCall *calls, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        SDL_FPoint origin, right, down;

        GetDrawCallCorners(&calls[i], &origin, &right, &down);
        if (!is_whole(calls[i].angle / 90.0) ||
            SDL_fabsf(origin.x - SDL_roundf(origin.x)) > 1e-4f || SDL_fabsf(origin.y - SDL_roundf(origin.y)) > 1e-4f ||
            SDL_fabsf(right.x - SDL_roundf(right.x)) > 1e-4f || SDL_fabsf(right.y - SDL_roundf(right.y)) > 1e-4f ||
            SDL_fabsf(down.x - SDL_roundf(down.x)) > 1e-4f || SDL_fabsf(down.y - SDL_roundf(down.y)) > 1e-4f) {
            return false;
        }
    }
    return true;
}

/* Whether a case can be checked with this renderer, counting the ones that can't */
static bool can_check(const DrawCall *calls, int count)
{
    if (hardware || is_exact(calls, count)) {
        return true;
    }
    skipped++;
    return false;
}

typedef struct image_case
{
    int w, h;
    int left, top, right, bottom;   /* trim */
    int pivot_x, pivot_y;
} image_case;

/* A sheet of one frame, whose atlas is made by pack() */
typedef struct canvas
{
    SPRAT_Atlas atlas;
    SPRAT_Frame frame;
    SPRAT_Sheet sheet;
} canvas;

static canvas *make_canvas(void)
{
    canvas *c = (canvas *)SDL_calloc(1, sizeof(canvas));

    c->sheet.num_atlases = 1;
    c->sheet.atlases = &c->atlas;
    c->sheet.num_frames = 1;
    c->sheet.frames = &c->frame;
    c->sheet.scale = 1.0f;
    return c;
}

static void destroy_canvas(canvas *c)
{
    SDL_DestroyTexture(c->atlas.texture);
    SDL_free(c);
}

/* A distinct, opaque value for every pixel of the original image; 0 is "nothing drawn" */
static Uint32 original_pixel(int x, int y)
{
    return ((Uint32)(y * 64 + x + 1) << 8) | 0xFF;
}

/* Packs the trimmed image at (3, 5) of a new atlas texture, turned like spratpack:
     CW (90):   atlas(col, row) <- content(row, h - 1 - col)
     CCW (270): atlas(col, row) <- content(w - 1 - row, col)
   and sets the canvas' frame to the one sprat-sdl would load for it */
static void pack(canvas *c, const image_case *img, int rotation, SPRAT_Slice slice)
{
    int cw = img->w - img->left - img->right;
    int ch = img->h - img->top - img->bottom;
    int rw = (rotation == 0) ? cw : ch;
    int rh = (rotation == 0) ? ch : cw;
    SDL_Surface *atlas = SDL_CreateSurface(ATLAS_SIZE, ATLAS_SIZE, SDL_PIXELFORMAT_RGBA8888);
    int row, col;

    SDL_ClearSurface(atlas, 0.0f, 0.0f, 0.0f, 0.0f);
    for (row = 0; row < rh; row++) {
        for (col = 0; col < rw; col++) {
            int px = (rotation == 90) ? row : (rotation == 270) ? cw - 1 - row : col;
            int py = (rotation == 90) ? ch - 1 - col : (rotation == 270) ? col : row;
            Uint32 *pixels = (Uint32 *)((Uint8 *)atlas->pixels + (5 + row) * atlas->pitch);
            pixels[3 + col] = original_pixel(img->left + px, img->top + py);
        }
    }

    SDL_DestroyTexture(c->atlas.texture);
    c->atlas.texture = SDL_CreateTextureFromSurface(renderer, atlas);
    SDL_SetTextureScaleMode(c->atlas.texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(c->atlas.texture, SDL_BLENDMODE_NONE);
    SDL_DestroySurface(atlas);

    SDL_zero(c->frame);
    c->frame.source.x = 3;
    c->frame.source.y = 5;
    c->frame.source.w = (float)rw;
    c->frame.source.h = (float)rh;
    c->frame.rotation = rotation;
    c->frame.w = (float)img->w;
    c->frame.h = (float)img->h;
    c->frame.content.x = (float)img->left;
    c->frame.content.y = (float)img->top;
    c->frame.content.w = (float)cw;
    c->frame.content.h = (float)ch;
    c->frame.pivot.x = (float)img->pivot_x;
    c->frame.pivot.y = (float)img->pivot_y;
    c->frame.slice = slice;
}

/* What belongs at screen point (sx, sy) of a render being checked */
typedef Uint32 (*expected_func)(const void *params, double sx, double sy);

/* Count the screen pixels that differ from what belongs there. A pixel whose center is on an
   edge, of the image or of a source pixel, may show either side: when the center doesn't match,
   points around it are tried too */
static int count_mismatches(expected_func expected, const void *params)
{
    static const double offsets[4][2] = { { -0.01, -0.01 }, { 0.01, -0.01 }, { -0.01, 0.01 }, { 0.01, 0.01 } };
    int mismatches = 0;
    int x, y, i;

    for (y = 0; y < SCREEN; y++) {
        for (x = 0; x < SCREEN; x++) {
            Uint32 got = screen[y][x];
            bool matched = (got == expected(params, x + 0.5, y + 0.5));

            for (i = 0; !matched && i < 4; i++) {
                matched = (got == expected(params, x + 0.5 + offsets[i][0], y + 0.5 + offsets[i][1]));
            }
            if (!matched) {
                mismatches++;
            }
        }
    }
    return mismatches;
}

/* A frame scaled, flipped about its pivot and turned clockwise around it, with the pivot at (x, y) */
typedef struct frame_render
{
    const image_case *img;
    double x, y;
    SDL_FPoint scale;
    double cos_angle, sin_angle;    /* of the angle that undoes the turn */
    SDL_FlipMode flip;
} frame_render;

/* The original pixel that belongs at screen point (sx, sy), 0 where the trim removed it */
static Uint32 expected_frame_pixel(const void *params, double sx, double sy)
{
    const frame_render *r = (const frame_render *)params;
    const image_case *img = r->img;
    double dx = sx - r->x;
    double dy = sy - r->y;
    double x = (dx * r->cos_angle - dy * r->sin_angle) / r->scale.x;
    double y = (dx * r->sin_angle + dy * r->cos_angle) / r->scale.y;

    if (r->flip & SDL_FLIP_HORIZONTAL) {
        x = -x;
    }
    if (r->flip & SDL_FLIP_VERTICAL) {
        y = -y;
    }
    x += img->pivot_x;
    y += img->pivot_y;
    if (x < img->left || x >= img->w - img->right || y < img->top || y >= img->h - img->bottom) {
        return 0;
    }
    return original_pixel((int)SDL_floor(x), (int)SDL_floor(y));
}

/* A sprite of a canvas's frame */
static SPRAT_Sprite canvas_sprite(const canvas *c, float x, float y, float scale_x, float scale_y, double angle, SDL_FlipMode flip)
{
    SPRAT_Sprite t = placed(x, y, scale_x, scale_y, angle, flip);

    t.sheet = &c->sheet;
    return t;
}

static void test_render_matches_original(void)
{
    static const image_case images[] = {
        { 9, 6, 1, 2, 3, 1, 6, 2 },     /* odd content size */
        { 9, 6, 1, 2, 3, 1, 0, 0 },     /* no pivot */
        { 10, 6, 1, 2, 3, 0, 5, 3 },    /* sprat-lib's README example */
        { 7, 11, 0, 0, 0, 0, 3, 10 },   /* untrimmed, pivot at the feet */
    };
    static const int rotations[] = { 0, 90, 270 };
    static const SDL_FPoint scales[] = { { 1, 1 }, { 3, 3 }, { 2, 3 }, { 1.5f, 2.25f } };
    static const double turns[] = { 0, 90, 180, 270, 33 };
    static const SDL_FPoint positions[] = { { 80, 70 }, { 80.25f, 69.6f } };
    static const SPRAT_Slice no_slice = { 0, 0, 0, 0, SPRAT_SLICEMODE_STRETCH, SPRAT_SLICEMODE_STRETCH };
    canvas *c = make_canvas();
    int checked = 0;
    size_t i, r, s, t, p;
    int flip;

    for (i = 0; i < COUNT(images); i++)
    for (r = 0; r < COUNT(rotations); r++) {
        pack(c, &images[i], rotations[r], no_slice);

        for (flip = 0; flip < 4; flip++)
        for (s = 0; s < COUNT(scales); s++)
        for (t = 0; t < COUNT(turns); t++)
        for (p = 0; p < COUNT(positions); p++) {
            SPRAT_Sprite sprite = canvas_sprite(c, positions[p].x, positions[p].y, scales[s].x, scales[s].y, turns[t], (SDL_FlipMode)flip);
            DrawCall call = GetSpriteDrawCall(&sprite);
            frame_render params;
            int mismatches;

            if (!can_check(&call, 1)) {
                continue;
            }

            params.img = &images[i];
            params.x = positions[p].x;
            params.y = positions[p].y;
            params.scale = scales[s];
            params.cos_angle = SDL_cos(-turns[t] * SDL_PI_D / 180.0);
            params.sin_angle = SDL_sin(-turns[t] * SDL_PI_D / 180.0);
            params.flip = (SDL_FlipMode)flip;

            clear();
            CHECK(SPRAT_RenderSprite(renderer, &sprite));
            read_screen();
            checked++;

            mismatches = count_mismatches(expected_frame_pixel, &params);
            if (mismatches != 0) {
                fprintf(stderr, "image %u rotation %d flip %d scale %gx%g turn %g at %g,%g: %d pixels differ\n",
                        (unsigned)i, rotations[r], flip, scales[s].x, scales[s].y, turns[t], positions[p].x, positions[p].y, mismatches);
                failures++;
            }
        }
    }
    CHECK(checked > 0);

    /* A negative scale mirrors, as a flip does */
    {
        SPRAT_Sprite mirrored = canvas_sprite(c, 80.0f, 70.0f, -2.0f, -3.0f, 0.0, SDL_FLIP_NONE);
        frame_render params;

        pack(c, &images[2], 90, no_slice);
        params.img = &images[2];
        params.x = 80.0;
        params.y = 70.0;
        params.scale.x = 2.0f;
        params.scale.y = 3.0f;
        params.cos_angle = 1.0;
        params.sin_angle = 0.0;
        params.flip = SDL_FLIP_HORIZONTAL_AND_VERTICAL;
        clear();
        CHECK(SPRAT_RenderSprite(renderer, &mirrored));
        read_screen();
        CHECK_INT(count_mismatches(expected_frame_pixel, &params), 0);
    }

    /* SPRAT_RenderFrame() is the unscaled, unturned case */
    pack(c, &images[2], 90, no_slice);
    clear();
    CHECK(SPRAT_RenderFrame(renderer, &c->sheet, 0, 20.0f, 30.0f));
    read_screen();
    CHECK_INT(screen[30 + 4 - 3][20 + 6 - 5], original_pixel(6, 4));
    CHECK_INT(screen[30 - 3][20 - 5], 0);   /* trimmed away */

    /* Nothing is rendered for a bad frame or an atlas that is not loaded */
    CHECK(!SPRAT_RenderFrame(renderer, &c->sheet, 1, 0.0f, 0.0f));
    CHECK_ERROR("Frame 1 is out of range");
    CHECK(!SPRAT_RenderFrame(renderer, NULL, 0, 0.0f, 0.0f));
    CHECK_ERROR("sheet");
    CHECK(!SPRAT_RenderSprite(renderer, &(SPRAT_Sprite){ &c->sheet, -1, 0.0f, 0.0f, 1.0f, 1.0f, 0.0, SDL_FLIP_NONE }));
    CHECK_ERROR("Frame -1 is out of range");
    CHECK(!SPRAT_RenderSprite(renderer, NULL));
    CHECK_ERROR("sprite");
    c->frame.atlas = 1;     /* past the sheet's atlases */
    CHECK(!SPRAT_RenderFrame(renderer, &c->sheet, 0, 0.0f, 0.0f));
    CHECK_ERROR("Atlas 1 of the sprite sheet is not loaded");
    c->frame.atlas = 0;
    SDL_DestroyTexture(c->atlas.texture);
    c->atlas.texture = NULL;
    CHECK(!SPRAT_RenderFrame(renderer, &c->sheet, 0, 0.0f, 0.0f));
    CHECK_ERROR("Atlas 0 of the sprite sheet is not loaded");

    destroy_canvas(c);
}

static void test_render_animation(void)
{
    static const image_case img = { 10, 6, 1, 2, 3, 0, 5, 3 };
    static const SPRAT_Slice no_slice = { 0, 0, 0, 0, SPRAT_SLICEMODE_STRETCH, SPRAT_SLICEMODE_STRETCH };
    static const int frames[] = { 0 };
    static const SPRAT_Animation mirrored = { "mirrored", 10, frames, 1, SDL_FLIP_HORIZONTAL, SPRAT_LOOPMODE_REPEAT };
    /* Whole-pixel geometry, which the software renderer also draws exactly */
    static const struct { float x, y, scale_x, scale_y; double angle; SDL_FlipMode flip; } transforms[] = {
        { 40.0f, 30.0f, 1.0f, 1.0f, 0.0, SDL_FLIP_NONE },
        { 40.0f, 30.0f, 3.0f, 2.0f, 90.0, SDL_FLIP_HORIZONTAL },
        { 50.0f, 40.0f, -2.0f, 2.0f, 180.0, SDL_FLIP_VERTICAL },
        { 50.0f, 40.0f, 2.0f, -1.0f, 270.0, (SDL_FlipMode)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL) },
    };
    static Uint32 expected_screen[SCREEN][SCREEN];
    SPRAT_AnimationPlayer player, none;
    canvas *c = make_canvas();
    size_t i;

    /* The animation's flip mirrors the frame around its pivot */
    pack(c, &img, 270, no_slice);
    clear();
    SPRAT_InitAnimationPlayer(&player, &mirrored);
    CHECK(SPRAT_RenderAnimation(renderer, &c->sheet, &player, 40.0f, 30.0f));
    read_screen();
    CHECK_INT(screen[30 + 4 - 3][40 - (6 - 5) - 1], original_pixel(6, 4));
    CHECK_INT(screen[30 + 4 - 3][40 + (6 - 5)], original_pixel(5 - (6 - 5) - 1, 4));   /* (3, 4), mirrored here */

    /* Rendered transformed, as the sprite of its frame, the flips combined: flipping the
       mirrored animation horizontally renders it unmirrored */
    for (i = 0; i < COUNT(transforms); i++) {
        const SPRAT_Sprite expected = canvas_sprite(c, transforms[i].x, transforms[i].y, transforms[i].scale_x, transforms[i].scale_y,
                                                    transforms[i].angle, (SDL_FlipMode)(transforms[i].flip ^ SDL_FLIP_HORIZONTAL));

        clear();
        CHECK(SPRAT_RenderSprite(renderer, &expected));
        read_screen();
        SDL_memcpy(expected_screen, screen, sizeof(screen));
        clear();
        CHECK(SPRAT_RenderAnimationRotated(renderer, &c->sheet, &player, transforms[i].x, transforms[i].y,
                                           transforms[i].scale_x, transforms[i].scale_y, transforms[i].angle, transforms[i].flip));
        read_screen();
        CHECK(SDL_memcmp(expected_screen, screen, sizeof(screen)) == 0);
    }
    clear();
    CHECK(SPRAT_RenderAnimationRotated(renderer, &c->sheet, &player, 40.0f, 30.0f, 1.0f, 1.0f, 0.0, SDL_FLIP_HORIZONTAL));
    read_screen();
    CHECK_INT(screen[30 + 4 - 3][40 + (6 - 5)], original_pixel(6, 4));     /* unmirrored */

    /* An animation without frames renders nothing and succeeds; no player fails */
    SPRAT_InitAnimationPlayer(&none, NULL);
    CHECK(SPRAT_RenderAnimationRotated(renderer, &c->sheet, &none, 40.0f, 30.0f, 1.0f, 1.0f, 0.0, SDL_FLIP_NONE));
    CHECK(!SPRAT_RenderAnimationRotated(renderer, &c->sheet, NULL, 40.0f, 30.0f, 1.0f, 1.0f, 0.0, SDL_FLIP_NONE));
    CHECK_ERROR("player");

    /* A frame that can't be rendered fails the animation's render */
    CHECK(!SPRAT_RenderAnimation(renderer, NULL, &player, 40.0f, 30.0f));
    CHECK_ERROR("sheet");

    destroy_canvas(c);
}

static void test_sprite_point_matches_render(void)
{
    static const image_case img = { 10, 6, 1, 2, 3, 0, 5, 3 };
    static const SPRAT_Slice no_slice = { 0, 0, 0, 0, SPRAT_SLICEMODE_STRETCH, SPRAT_SLICEMODE_STRETCH };
    SDL_FPoint inside = { 6.5f, 4.5f };
    SDL_FPoint p = { 0, 0 };
    SPRAT_Sprite t;
    canvas *c = make_canvas();

    pack(c, &img, 90, no_slice);

    /* The pivot lands on the position, however the sprite is scaled, turned and flipped */
    t = canvas_sprite(c, 40, 30, 2, 3, 33.0, SDL_FLIP_HORIZONTAL);
    CHECK(SPRAT_GetSpritePoint(&t, &c->frame.pivot, &p));
    CHECK_FLOAT(p.x, 40);
    CHECK_FLOAT(p.y, 30);

    /* A point inside content pixel (6, 4) lands on the screen pixel that shows it */
    t = canvas_sprite(c, 80, 70, 2, 2, 90.0, SDL_FLIP_HORIZONTAL);
    CHECK(SPRAT_GetSpritePoint(&t, &inside, &p));
    clear();
    CHECK(SPRAT_RenderSprite(renderer, &t));
    read_screen();
    CHECK_INT(screen[(int)SDL_floor(p.y)][(int)SDL_floor(p.x)], original_pixel(6, 4));

    /* At any angle, too */
    t = canvas_sprite(c, 80, 70, 3, 3, 33.0, SDL_FLIP_NONE);
    CHECK(SPRAT_GetSpritePoint(&t, &inside, &p));
    clear();
    CHECK(SPRAT_RenderSprite(renderer, &t));
    read_screen();
    CHECK_INT(screen[(int)SDL_floor(p.y)][(int)SDL_floor(p.x)], original_pixel(6, 4));

    /* A negative scale mirrors the point as a flip does, and as it mirrors the render */
    {
        SDL_FPoint flipped = { 0, 0 };
        SPRAT_Sprite mirrored = canvas_sprite(c, 80, 70, -2, 3, 33.0, SDL_FLIP_VERTICAL);
        SPRAT_Sprite both_flips = canvas_sprite(c, 80, 70, 2, 3, 33.0, SDL_FLIP_HORIZONTAL_AND_VERTICAL);

        CHECK(SPRAT_GetSpritePoint(&mirrored, &inside, &p));
        CHECK(SPRAT_GetSpritePoint(&both_flips, &inside, &flipped));
        CHECK_FLOAT(p.x, flipped.x);
        CHECK_FLOAT(p.y, flipped.y);
        t = canvas_sprite(c, 80, 70, -2, -2, 90.0, SDL_FLIP_NONE);
        CHECK(SPRAT_GetSpritePoint(&t, &inside, &p));
        clear();
        CHECK(SPRAT_RenderSprite(renderer, &t));
        read_screen();
        CHECK_INT(screen[(int)SDL_floor(p.y)][(int)SDL_floor(p.x)], original_pixel(6, 4));
    }

    /* Quarter turns are exact: whole points stay whole */
    t = canvas_sprite(c, 80, 70, 1, 1, 90.0, SDL_FLIP_NONE);
    CHECK(SPRAT_GetSpritePoint(&t, &inside, &p));
    CHECK(p.x == 80.0f - (4.5f - 3.0f) && p.y == 70.0f + (6.5f - 5.0f));

    CHECK(!SPRAT_GetSpritePoint(NULL, &inside, &p));
    CHECK_ERROR("sprite");
    t = canvas_sprite(c, 9, 8, 1, 1, 0.0, SDL_FLIP_NONE);
    t.sheet = NULL;
    CHECK(!SPRAT_GetSpritePoint(&t, &inside, &p));
    CHECK_ERROR("sheet");
    t = canvas_sprite(c, 9, 8, 1, 1, 0.0, SDL_FLIP_NONE);
    t.frame = 1;
    CHECK(!SPRAT_GetSpritePoint(&t, &inside, &p));
    CHECK_ERROR("Frame 1 is out of range");
    t.frame = 0;
    CHECK(!SPRAT_GetSpritePoint(&t, NULL, &p));
    CHECK_ERROR("point");
    CHECK(!SPRAT_GetSpritePoint(&t, &inside, NULL));
    CHECK_ERROR("result");

    destroy_canvas(c);
}

/* A frame rendered as a nine-slice into dest */
typedef struct grid_render
{
    const image_case *img;
    SPRAT_Slice slice;
    float scale;
    SDL_FRect dest;
} grid_render;

/* The original pixel a nine-slice puts at screen point (sx, sy) */
static Uint32 expected_9grid_pixel(const void *params, double sx, double sy)
{
    const grid_render *r = (const grid_render *)params;
    const image_case *img = r->img;
    double source[2][4];
    double start[2], length[2], coord[2], out[2];
    double x, y;
    int axis;

    source[0][0] = 0;
    source[0][1] = r->slice.left;
    source[0][2] = (double)img->w - r->slice.right;
    source[0][3] = img->w;
    source[1][0] = 0;
    source[1][1] = r->slice.top;
    source[1][2] = (double)img->h - r->slice.bottom;
    source[1][3] = img->h;
    start[0] = r->dest.x;
    start[1] = r->dest.y;
    length[0] = r->dest.w;
    length[1] = r->dest.h;
    coord[0] = sx;
    coord[1] = sy;

    for (axis = 0; axis < 2; axis++) {
        double before = source[axis][1] * r->scale, after = (source[axis][3] - source[axis][2]) * r->scale;
        double edges[4];
        double c = coord[axis];
        int cell;

        if (before + after > length[axis]) {
            double k = length[axis] / (before + after);
            before *= k;
            after *= k;
        }
        edges[0] = start[axis];
        edges[1] = start[axis] + before;
        edges[2] = start[axis] + length[axis] - after;
        edges[3] = start[axis] + length[axis];
        if (c < edges[0] || c >= edges[3]) {
            return 0;
        }
        cell = (c < edges[1]) ? 0 : (c < edges[2]) ? 1 : 2;
        out[axis] = source[axis][cell] + (c - edges[cell]) / (edges[cell + 1] - edges[cell]) * (source[axis][cell + 1] - source[axis][cell]);
    }

    x = out[0];
    y = out[1];
    if (x < img->left || x >= img->w - img->right || y < img->top || y >= img->h - img->bottom) {
        return 0;
    }
    return original_pixel((int)SDL_floor(x), (int)SDL_floor(y));
}

/* The upright rectangle a draw call fills: a quarter-turned call fills its rectangle turned */
static SDL_FRect get_filled_rect(const DrawCall *call)
{
    SDL_FRect rect = call->dest;

    if (!is_whole(call->angle / 180.0)) {
        rect.x += (rect.w - rect.h) / 2.0f;
        rect.y += (rect.h - rect.w) / 2.0f;
        rect.w = call->dest.h;
        rect.h = call->dest.w;
    }
    return rect;
}

/* Count the pairs of draw calls that fill the same pixels, which a translucent atlas would
   show twice */
static int count_overlaps(const DrawCall *calls, int count)
{
    int overlaps = 0;
    int i, j;

    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            SDL_FRect a = get_filled_rect(&calls[i]);
            SDL_FRect b = get_filled_rect(&calls[j]);
            float w = SDL_min(a.x + a.w, b.x + b.w) - SDL_max(a.x, b.x);
            float h = SDL_min(a.y + a.h, b.y + b.h) - SDL_max(a.y, b.y);

            if (w > 1e-3f && h > 1e-3f) {
                overlaps++;
            }
        }
    }
    return overlaps;
}

static void test_9grid_matches_original(void)
{
    static const image_case images[] = {
        { 12, 10, 0, 0, 0, 0, 0, 0 },   /* untrimmed */
        { 12, 10, 1, 3, 4, 2, 0, 0 },   /* trimmed into the borders */
        { 12, 10, 5, 4, 5, 4, 0, 0 },   /* trimmed past the borders: only middle content left */
    };
    static const SPRAT_Slice slices[] = {
        { 3, 2, 4, 3, SPRAT_SLICEMODE_STRETCH, SPRAT_SLICEMODE_STRETCH },
        { 0, 0, 0, 0, SPRAT_SLICEMODE_STRETCH, SPRAT_SLICEMODE_STRETCH },   /* no slice: plain stretch */
    };
    static const SDL_FRect dests[] = {
        { 10, 20, 12, 10 },             /* natural size */
        { 10, 20, 47, 35 },             /* middle 5x5 grows to 40x30 */
        { 10, 20, 112, 95 },            /* 105 = 7*15 wide, 90 = 18*5 high: exact for both slices */
        { 10, 20, 72, 30 },             /* 6x and 3x without slice, middle 13x and 5x with it */
        { 10.5f, 20.25f, 61.3f, 47.7f }, /* off the pixel grid */
        { 10, 20, 7, 4 },               /* smaller than the borders: they shrink */
    };
    static const float corner_scales[] = { 0.0f, 2.0f, 1.5f };
    static const int rotations[] = { 0, 90, 270 };
    canvas *c = make_canvas();
    int checked = 0;
    size_t i, sl, d, k, r;

    for (i = 0; i < COUNT(images); i++)
    for (r = 0; r < COUNT(rotations); r++)
    for (sl = 0; sl < COUNT(slices); sl++) {
        pack(c, &images[i], rotations[r], slices[sl]);

        for (d = 0; d < COUNT(dests); d++)
        for (k = 0; k < COUNT(corner_scales); k++) {
            DrawCall calls[9];
            int count = Get9GridDrawCalls(&c->frame, corner_scales[k], &dests[d], calls);
            int mismatches, overlaps = count_overlaps(calls, count);
            grid_render params;

            if (overlaps != 0) {
                fprintf(stderr, "9-grid image %u slice %u dest %u scale %g rotation %d: %d parts overlap\n",
                        (unsigned)i, (unsigned)sl, (unsigned)d, corner_scales[k], rotations[r], overlaps);
                failures++;
            }
            if (!can_check(calls, count)) {
                continue;
            }

            params.img = &images[i];
            params.slice = slices[sl];
            params.scale = (corner_scales[k] > 0.0f) ? corner_scales[k] : 1.0f;
            params.dest = dests[d];

            clear();
            CHECK(SPRAT_RenderFrame9Grid(renderer, &c->sheet, 0, corner_scales[k], &dests[d]));
            read_screen();
            checked++;

            mismatches = count_mismatches(expected_9grid_pixel, &params);
            if (mismatches != 0) {
                fprintf(stderr, "9-grid image %u slice %u dest %u scale %g rotation %d: %d pixels differ\n",
                        (unsigned)i, (unsigned)sl, (unsigned)d, corner_scales[k], rotations[r], mismatches);
                failures++;
            }
        }
    }
    CHECK(checked > 0);

    /* Nothing to draw into an empty rectangle, and a rectangle is required */
    {
        SDL_FRect empty = { 0, 0, 0, 10 };
        DrawCall calls[9];

        pack(c, &images[0], 0, slices[0]);
        CHECK_INT(Get9GridDrawCalls(&c->frame, 0.0f, &empty, calls), 0);
        CHECK(SPRAT_RenderFrame9Grid(renderer, &c->sheet, 0, 0.0f, &empty));
        CHECK(!SPRAT_RenderFrame9Grid(renderer, &c->sheet, 0, 0.0f, NULL));
        CHECK_ERROR("dstrect");
        empty.w = -5.0f;
        CHECK_INT(Get9GridDrawCalls(&c->frame, 0.0f, &empty, calls), 0);
        CHECK(!SPRAT_RenderFrame9Grid(renderer, &c->sheet, 1, 0.0f, &dests[0]));
        CHECK_ERROR("Frame 1 is out of range");
        CHECK(!SPRAT_RenderFrame9Grid(renderer, NULL, 0, 0.0f, &dests[0]));
        CHECK_ERROR("sheet");
        SDL_DestroyTexture(c->atlas.texture);
        c->atlas.texture = NULL;
        CHECK(!SPRAT_RenderFrame9Grid(renderer, &c->sheet, 0, 0.0f, &dests[0]));
        CHECK_ERROR("Atlas 0 of the sprite sheet is not loaded");
    }

    destroy_canvas(c);
}

/* Every frame of the example sheet, loaded with its atlas and rendered at its
   pivot, shows the source image it was packed from, pixel for pixel */
static void test_example_renders_its_sources(void)
{
    SPRAT_Sheet *sheet = SPRAT_LoadSheet(renderer, SPRAT_SDL_EXAMPLE_SHEET);
    char *resources = NULL;
    int i;

    CHECK(sheet != NULL);
    if (!sheet) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return;
    }
    CHECK(sheet->atlases[0].texture != NULL);
    SDL_SetTextureScaleMode(sheet->atlases[0].texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(sheet->atlases[0].texture, SDL_BLENDMODE_NONE);
    CHECK(GetDirectory(SPRAT_SDL_EXAMPLE_SHEET, &resources));

    for (i = 0; i < sheet->num_frames; i++) {
        const SPRAT_Frame *frame = &sheet->frames[i];
        char *path = NULL;
        SDL_Surface *loaded, *source;
        int mismatches = 0;
        int x, y;
        int left = 8, top = 8;      /* where the source image's top-left corner goes */

        SDL_asprintf(&path, "%s/sources/%s", resources, frame->path);
        loaded = SDL_LoadSurface(path);
        CHECK(loaded != NULL);
        if (!loaded) {
            fprintf(stderr, "%s\n", SDL_GetError());
            SDL_free(path);
            continue;
        }
        source = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA8888);
        SDL_DestroySurface(loaded);

        /* sprat-cli strips the 1 pixel guide border of a .9.png */
        if (SDL_strstr(frame->path, ".9.png")) {
            SDL_Rect inner = { 1, 1, source->w - 2, source->h - 2 };
            SDL_Surface *cropped = SDL_CreateSurface(inner.w, inner.h, SDL_PIXELFORMAT_RGBA8888);
            SDL_SetSurfaceBlendMode(source, SDL_BLENDMODE_NONE);
            SDL_BlitSurface(source, &inner, cropped, NULL);
            SDL_DestroySurface(source);
            source = cropped;
        }

        clear();
        CHECK(SPRAT_RenderFrame(renderer, sheet, i, (float)left + frame->pivot.x, (float)top + frame->pivot.y));
        read_screen();

        /* Fully transparent pixels may hold any color, in the source or in the atlas */
        for (y = 0; y < SCREEN; y++) {
            for (x = 0; x < SCREEN; x++) {
                Uint32 got = screen[y][x];
                Uint32 want = 0;
                int sx = x - left, sy = y - top;

                if (sx >= 0 && sy >= 0 && sx < source->w && sy < source->h) {
                    want = ((const Uint32 *)((const Uint8 *)source->pixels + sy * source->pitch))[sx];
                }
                if ((got & 0xFF) == 0 && (want & 0xFF) == 0) {
                    continue;
                }
                if (got != want) {
                    mismatches++;
                }
            }
        }

        if (mismatches != 0) {
            fprintf(stderr, "example frame %s: %d pixels differ from %s\n", frame->path, mismatches, path);
            failures++;
        }
        SDL_DestroySurface(source);
        SDL_free(path);
    }

    SDL_free(resources);
    SPRAT_DestroySheet(sheet);
}

static void test_version(void)
{
    CHECK_INT(SPRAT_Version(), SDL_SPRAT_VERSION);
    CHECK(SDL_SPRAT_VERSION_ATLEAST(0, 1, 0));
    CHECK(!SDL_SPRAT_VERSION_ATLEAST(1, 0, 0));
}

int main(void)
{
    hook_memory_functions();

    if (!create_renderer()) {
        return 1;
    }

    test_version();
    test_load_example();
    test_load_polygon_and_empty();
    test_load_loop_modes();
    test_alias_frames_are_shared();
    test_add_size_overflow();
    test_paths();
    test_load_errors();
    test_enums_match_sprat();
    test_load_slices_and_markers();
    test_load_atlases();
    test_load_out_of_memory();
    test_frame_names();
    test_lookups();
    test_animation_player();
    test_animation_pingpong();
    test_set_animation();
    test_animation_sprite();
    test_markers_overlap();
    test_markers_overlap_matches_reference();
    test_markers_overlap_at_threshold();
    test_marker_center();
    test_marker_outline();
    test_render_matches_original();
    test_render_animation();
    test_sprite_point_matches_render();
    test_9grid_matches_original();
    test_example_renders_its_sources();

    if (!hardware) {
        const char *require = SDL_getenv("SPRAT_TEST_REQUIRE_HARDWARE");

        printf("no hardware renderer: %d renders off the pixel grid were not checked\n", skipped);
        /* CI sets it to 1 where a hardware renderer must be there, so no case goes unchecked */
        if (require && *require && SDL_strcmp(require, "0") != 0) {
            fprintf(stderr, "SPRAT_TEST_REQUIRE_HARDWARE is set, but there is no hardware renderer\n");
            failures++;
        }
    }

    destroy_renderer();
    SDL_Quit();

    if (failures != 0) {
        fprintf(stderr, "%d failures\n", failures);
    } else {
        printf("all tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
