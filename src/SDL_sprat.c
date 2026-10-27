/*
  sprat-sdl: load and render sprat-cli sprite sheets with SDL3
  Copyright (C) 2026 Pedro Amaral Couto

  MIT License. See LICENSE.
*/

#include <SDL3_sprat/SDL_sprat.h>

#include <SDL3/SDL.h>

#include "sprat/sprat.h"

/* Version checks */
#if (SDL_SPRAT_MAJOR_VERSION < 0 || SDL_SPRAT_MINOR_VERSION < 0 || SDL_SPRAT_MICRO_VERSION < 0)
#error Invalid sprat-sdl version
#endif

#if !SDL_VERSION_ATLEAST(3, 4, 0)
#error sprat-sdl requires SDL 3.4.0 or newer
#endif

/* The arguments of one SDL_RenderTextureRotated() call: an atlas region, the rectangle it fills,
   and the clockwise angle and flip applied around the rectangle's center */
typedef struct DrawCall
{
    SDL_FRect source;
    SDL_FRect dest;
    double angle;
    SDL_FlipMode flip;
} DrawCall;

/* The size of the buffers that hold escaped text for error messages; longer text is cut short */
#define ESCAPED_SIZE 256

/* Escape names and paths for an error message with sprat_escape(): they come from the sheet,
   and could otherwise put terminal escape sequences or reordered text into an application's log */
static const char *Escape(char *buffer, const char *text)
{
    sprat_escape(buffer, ESCAPED_SIZE, text);
    return buffer;
}

int SPRAT_Version(void)
{
    return SDL_SPRAT_VERSION;
}

/* Round a section size up so the next section stays aligned for any member */
static size_t Align(size_t size)
{
    return (size + 15) & ~(size_t)15;
}

/* Add count items of item_size bytes to a size, as one aligned section when align is true.
   Returns false instead of wrapping around, which on a 32-bit size_t would undersize the block */
static bool AddSize(size_t *total, size_t count, size_t item_size, bool align)
{
    size_t size;

    if (item_size > 0 && count > (SIZE_MAX - 15) / item_size) {
        return false;
    }

    size = count * item_size;
    if (align) {
        size = Align(size);
    }

    if (size > SIZE_MAX - *total) {
        return false;
    }

    *total += size;
    return true;
}

/* Copy a string into the sheet's string pool */
static const char *CopyString(char **pool, const char *text)
{
    size_t length = SDL_strlen(text) + 1;
    char *copy = *pool;

    SDL_memcpy(copy, text, length);
    *pool += length;

    return copy;
}

/* Copy a loaded sprat_sheet into a SPRAT_Sheet, without textures.
   Everything goes in one allocation that starts with the SPRAT_Sheet itself.
   The copy holds no pointer into raw, so it can be freed right after */
static SPRAT_Sheet *ConvertSheet(const sprat_sheet *raw)
{
    /* Counts: the loader bounds them by the input size, but a SPRAT_Sheet counts with int.
       Every sum is checked, so a hostile file can't wrap a size around and undersize the block */
    size_t marker_total = 0;
    size_t vertex_total = 0;
    size_t string_total = 0;
    size_t index_total = 0;
    const size_t *frame_base = NULL;
    bool fits = true;
    size_t i, m, v, f;

    size_t sheet_size = 0, atlases_size = 0, frames_size = 0, animations_size = 0;
    size_t markers_size = 0, vertices_size = 0, indexes_size = 0, total_size = 0;
    Uint8 *block, *section;
    SPRAT_Sheet *sheet;
    SPRAT_Atlas *atlases;
    SPRAT_Frame *frames;
    SPRAT_Animation *animations;
    SPRAT_Marker *markers;
    SDL_FPoint *vertices;
    int *indexes;
    char *strings;

    for (i = 0; fits && i < raw->atlas_count; i++) {
        fits = AddSize(&string_total, SDL_strlen(raw->atlases[i].path) + 1, 1, false);
    }

    for (i = 0; fits && i < raw->sprite_count; i++) {
        const sprat_sprite *sprite = &raw->sprites[i];

        fits = AddSize(&marker_total, sprite->marker_count, 1, false) &&
               AddSize(&string_total, SDL_strlen(sprite->name) + SDL_strlen(sprite->path) + 2, 1, false);

        for (m = 0; fits && m < sprite->marker_count; m++) {
            fits = AddSize(&vertex_total, sprite->markers[m].vertex_count, 1, false) &&
                   AddSize(&string_total, SDL_strlen(sprite->markers[m].name) + 1, 1, false);
        }
    }

    /* Frame indexes of regular animations are slices of one array of the sprat sheet, and an alias
       shares its source's slice: copy that array once, so aliases can't multiply its size */
    for (i = 0; fits && i < raw->animation_count; i++) {
        const sprat_animation *src = &raw->animations[i];

        fits = AddSize(&string_total, SDL_strlen(src->name) + 1, 1, false);

        if (src->alias_of == NULL && src->frame_count > 0) {
            fits = fits && AddSize(&index_total, src->frame_count, 1, false);
            if (frame_base == NULL || (uintptr_t)src->frames < (uintptr_t)frame_base) {
                frame_base = src->frames;
            }
        }
    }

    if (!fits || raw->atlas_count > SDL_MAX_SINT32 || raw->sprite_count > SDL_MAX_SINT32 ||
        raw->animation_count > SDL_MAX_SINT32 || marker_total > SDL_MAX_SINT32 ||
        vertex_total > SDL_MAX_SINT32 || index_total > SDL_MAX_SINT32) {
        SDL_SetError("Sprite sheet is too large");
        return NULL;
    }

    if (!AddSize(&sheet_size, 1, sizeof(SPRAT_Sheet), true) ||
        !AddSize(&atlases_size, raw->atlas_count, sizeof(SPRAT_Atlas), true) ||
        !AddSize(&frames_size, raw->sprite_count, sizeof(SPRAT_Frame), true) ||
        !AddSize(&animations_size, raw->animation_count, sizeof(SPRAT_Animation), true) ||
        !AddSize(&markers_size, marker_total, sizeof(SPRAT_Marker), true) ||
        !AddSize(&vertices_size, vertex_total, sizeof(SDL_FPoint), true) ||
        !AddSize(&indexes_size, index_total, sizeof(int), true) ||
        !AddSize(&total_size, sheet_size, 1, false) ||
        !AddSize(&total_size, atlases_size, 1, false) ||
        !AddSize(&total_size, frames_size, 1, false) ||
        !AddSize(&total_size, animations_size, 1, false) ||
        !AddSize(&total_size, markers_size, 1, false) ||
        !AddSize(&total_size, vertices_size, 1, false) ||
        !AddSize(&total_size, indexes_size, 1, false) ||
        !AddSize(&total_size, string_total, 1, false)) {
        SDL_SetError("Sprite sheet is too large");
        return NULL;
    }

    block = (Uint8 *)SDL_calloc(1, total_size);
    if (!block) {
        SDL_OutOfMemory();
        return NULL;
    }

    section = block;
    sheet = (SPRAT_Sheet *)section;             section += sheet_size;
    atlases = (SPRAT_Atlas *)section;           section += atlases_size;
    frames = (SPRAT_Frame *)section;            section += frames_size;
    animations = (SPRAT_Animation *)section;    section += animations_size;
    markers = (SPRAT_Marker *)section;          section += markers_size;
    vertices = (SDL_FPoint *)section;           section += vertices_size;
    indexes = (int *)section;                   section += indexes_size;
    strings = (char *)section;

    for (i = 0; i < raw->atlas_count; i++) {
        atlases[i].path = CopyString(&strings, raw->atlases[i].path);
        atlases[i].w = raw->atlases[i].width;
        atlases[i].h = raw->atlases[i].height;
    }

    for (i = 0; i < raw->sprite_count; i++) {
        const sprat_sprite *sprite = &raw->sprites[i];
        SPRAT_Frame *frame = &frames[i];
        sprat_size content = sprat_sprite_content_size(sprite);
        sprat_size size = sprat_sprite_source_size(sprite);

        frame->name = CopyString(&strings, sprite->name);
        frame->path = CopyString(&strings, sprite->path);
        frame->atlas = (int)sprite->atlas_index;
        frame->source.x = (float)sprite->rect.x;
        frame->source.y = (float)sprite->rect.y;
        frame->source.w = (float)sprite->rect.w;
        frame->source.h = (float)sprite->rect.h;
        frame->rotation = sprite->rotation;
        frame->w = (float)size.w;
        frame->h = (float)size.h;
        frame->content.x = (float)sprite->trim.left;
        frame->content.y = (float)sprite->trim.top;
        frame->content.w = (float)content.w;
        frame->content.h = (float)content.h;
        frame->pivot.x = (float)sprite->pivot.x;
        frame->pivot.y = (float)sprite->pivot.y;

        if (sprite->has_slice) {
            frame->slice.left = (float)sprite->slice.left;
            frame->slice.top = (float)sprite->slice.top;
            frame->slice.right = (float)sprite->slice.right;
            frame->slice.bottom = (float)sprite->slice.bottom;
            frame->slice.horizontal_mode = (SPRAT_SliceMode)sprite->slice.h_mode;
            frame->slice.vertical_mode = (SPRAT_SliceMode)sprite->slice.v_mode;
        }

        frame->markers = (sprite->marker_count > 0) ? markers : NULL;
        frame->num_markers = (int)sprite->marker_count;

        for (m = 0; m < sprite->marker_count; m++) {
            const sprat_marker *src = &sprite->markers[m];
            SPRAT_Marker *marker = markers++;

            marker->name = CopyString(&strings, src->name);
            marker->type = (SPRAT_MarkerType)src->type;
            marker->position.x = (float)src->x;
            marker->position.y = (float)src->y;
            marker->radius = (float)src->radius;
            marker->w = (float)src->w;
            marker->h = (float)src->h;
            marker->vertices = (src->vertex_count > 0) ? vertices : NULL;
            marker->num_vertices = (int)src->vertex_count;

            for (v = 0; v < src->vertex_count; v++) {
                vertices->x = (float)src->vertices[v].x;
                vertices->y = (float)src->vertices[v].y;
                vertices++;
            }
        }
    }

    for (i = 0; i < raw->animation_count; i++) {
        const sprat_animation *src = &raw->animations[i];
        SPRAT_Animation *anim = &animations[i];
        size_t offset;
        int *slice;

        anim->name = CopyString(&strings, src->name);
        anim->fps = src->fps;
        anim->num_frames = (int)src->frame_count;
        anim->flip = (SDL_FlipMode)src->flip;   /* SPRAT_FLIP_H and SPRAT_FLIP_V have SDL_FLIP_HORIZONTAL and SDL_FLIP_VERTICAL's values */
        anim->loop_mode = (SPRAT_LoopMode)src->loop;    /* SPRAT_LOOP_ONCE and SPRAT_LOOP_PINGPONG have SPRAT_LOOPMODE_ONCE and SPRAT_LOOPMODE_PINGPONG's values */

        if (src->frame_count == 0) {
            continue;
        }

        /* The same slice of the copy as of the sprat sheet's array, checked to lie inside the copy */
        offset = ((uintptr_t)src->frames - (uintptr_t)frame_base) / sizeof(size_t);

        if ((uintptr_t)src->frames < (uintptr_t)frame_base || offset > index_total || src->frame_count > index_total - offset) {
            SDL_free(block);
            SDL_SetError("Sprite sheet has an animation outside its frame list");
            return NULL;
        }

        slice = indexes + offset;
        anim->frames = slice;

        if (src->alias_of == NULL) {
            for (f = 0; f < src->frame_count; f++) {
                slice[f] = (int)src->frames[f];
            }
        }
    }

    sheet->num_atlases = (int)raw->atlas_count;
    sheet->atlases = (raw->atlas_count > 0) ? atlases : NULL;
    sheet->num_frames = (int)raw->sprite_count;
    sheet->frames = (raw->sprite_count > 0) ? frames : NULL;
    sheet->num_animations = (int)raw->animation_count;
    sheet->animations = (raw->animation_count > 0) ? animations : NULL;
    sheet->scale = (float)raw->scale;

    return sheet;
}

static bool IsAbsolutePath(const char *path)
{
    return path[0] == '/' || path[0] == '\\' ||
           (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':');
}

/* Join an atlas path with the folder it is relative to, in a new string to SDL_free().
   Absolute paths, and any path when dir is NULL, empty or ".", are kept as written */
static char *JoinPath(const char *dir, const char *path)
{
    char *result = NULL;
    size_t dir_length;

    if (IsAbsolutePath(path) || !dir || !*dir || SDL_strcmp(dir, ".") == 0) {
        return SDL_strdup(path);
    }

    dir_length = SDL_strlen(dir);
    if (dir[dir_length - 1] == '/' || dir[dir_length - 1] == '\\') {
        SDL_asprintf(&result, "%s%s", dir, path);
    } else {
        SDL_asprintf(&result, "%s/%s", dir, path);
    }
    return result;
}

/* Get the folder of a file, in a new string to SDL_free(), or NULL when it has none */
static bool GetDirectory(const char *file, char **dir)
{
    const char *slash = SDL_strrchr(file, '/');
    const char *backslash = SDL_strrchr(file, '\\');
    size_t length;

    *dir = NULL;

    if (backslash > slash) {
        slash = backslash;
    }
    if (!slash) {
        return true;
    }

    length = (slash == file) ? 1 : (size_t)(slash - file);  /* "/sheet.sprat" is in "/" */
    *dir = (char *)SDL_malloc(length + 1);
    if (!*dir) {
        return false;
    }

    SDL_memcpy(*dir, file, length);
    (*dir)[length] = '\0';
    return true;
}

/* Set an error that adds context before the current error */
static void PrefixError(const char *prefix, const char *name)
{
    char escaped_name[ESCAPED_SIZE], escaped_reason[ESCAPED_SIZE];

    /* Escaping copies the reason, which SDL_SetError() would overwrite while reading it */
    Escape(escaped_reason, SDL_GetError());
    SDL_SetError("%s %s: %s", prefix, Escape(escaped_name, name), escaped_reason);
}

/* Load the atlas textures of a sheet */
static bool LoadAtlases(SDL_Renderer *renderer, SPRAT_Sheet *sheet, const char *atlas_dir)
{
    int i;

    for (i = 0; i < sheet->num_atlases; i++) {
        SPRAT_Atlas *atlas = &sheet->atlases[i];
        SDL_Surface *surface;
        char *path = JoinPath(atlas_dir, atlas->path);

        if (!path) {
            return false;
        }

        surface = SDL_LoadSurface(path);
        if (!surface) {
            PrefixError("Couldn't load atlas", path);
            SDL_free(path);
            return false;
        }

        if (atlas->w > 0 && atlas->h > 0 && (surface->w != atlas->w || surface->h != atlas->h)) {
            char escaped[ESCAPED_SIZE];
            SDL_SetError("Atlas %s is %dx%d, the sprite sheet was packed for %dx%d", Escape(escaped, path), surface->w, surface->h, atlas->w, atlas->h);
            SDL_DestroySurface(surface);
            SDL_free(path);
            return false;
        }

        atlas->texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        SDL_free(path);

        if (!atlas->texture) {
            return false;
        }
    }

    return true;
}

/* Parse sheet data and load its atlas textures */
static SPRAT_Sheet *LoadSheetData(SDL_Renderer *renderer, const void *data, size_t size, const char *atlas_dir, const char *name)
{
    SPRAT_Sheet *sheet;
    sprat_sheet *raw = NULL;
    sprat_error error;
    char escaped[ESCAPED_SIZE];

    SDL_zero(error);

    if (sprat_sheet_load_memory((const char *)data, size, &raw, &error) != SPRAT_OK) {
        if (error.status == SPRAT_ERR_NOMEM) {
            SDL_OutOfMemory();
        } else if (error.line > 0) {
            SDL_SetError("Couldn't load sprite sheet %s: %u:%u: %s", Escape(escaped, name), (unsigned)error.line, (unsigned)error.column, error.message);
        } else {
            SDL_SetError("Couldn't load sprite sheet %s: %s", Escape(escaped, name), error.message);
        }
        return NULL;
    }

    sheet = ConvertSheet(raw);
    sprat_sheet_free(raw);

    if (sheet && renderer && !LoadAtlases(renderer, sheet, atlas_dir)) {
        SPRAT_DestroySheet(sheet);
        return NULL;
    }

    return sheet;
}

SPRAT_Sheet *SPRAT_LoadSheet(SDL_Renderer *renderer, const char *file)
{
    SPRAT_Sheet *sheet;
    char *atlas_dir;
    size_t size = 0;
    void *data;

    if (!file || !*file) {
        SDL_InvalidParamError("file");
        return NULL;
    }

    data = SDL_LoadFile(file, &size);
    if (!data) {
        PrefixError("Couldn't load sprite sheet", file);
        return NULL;
    }

    if (!GetDirectory(file, &atlas_dir)) {
        SDL_free(data);
        return NULL;
    }

    sheet = LoadSheetData(renderer, data, size, atlas_dir, file);
    SDL_free(atlas_dir);
    SDL_free(data);

    return sheet;
}

SPRAT_Sheet *SPRAT_LoadSheet_IO(SDL_Renderer *renderer, SDL_IOStream *src, bool closeio, const char *atlas_dir)
{
    SPRAT_Sheet *sheet;
    size_t size = 0;
    void *data;

    if (!src) {
        SDL_InvalidParamError("src");
        return NULL;
    }

    data = SDL_LoadFile_IO(src, &size, closeio);
    if (!data) {
        return NULL;
    }

    sheet = LoadSheetData(renderer, data, size, atlas_dir, "from stream");
    SDL_free(data);

    return sheet;
}

void SPRAT_DestroySheet(SPRAT_Sheet *sheet)
{
    int i;

    if (!sheet) {
        return;
    }

    for (i = 0; i < sheet->num_atlases; i++) {
        if (sheet->atlases[i].texture) {
            SDL_DestroyTexture(sheet->atlases[i].texture);
        }
    }

    /* The whole sheet lives in one allocation, starting at the SPRAT_Sheet */
    SDL_free(sheet);
}

/* Check a sheet and a frame index, setting the error when they are not valid */
static bool CheckFrame(const SPRAT_Sheet *sheet, int frame)
{
    if (!sheet) {
        return SDL_InvalidParamError("sheet");
    }
    if (frame < 0 || frame >= sheet->num_frames) {
        return SDL_SetError("Frame %d is out of range: the sprite sheet has %d", frame, sheet->num_frames);
    }
    return true;
}

/* Check a sprite, its sheet and its frame, setting the error when they are not valid */
static bool CheckSprite(const SPRAT_Sprite *sprite, const char *param)
{
    if (!sprite) {
        return SDL_InvalidParamError(param);
    }
    return CheckFrame(sprite->sheet, sprite->frame);
}

/* Check a frame and the atlas texture it is rendered from */
static SDL_Texture *GetFrameTexture(const SPRAT_Sheet *sheet, int frame)
{
    int atlas;

    if (!CheckFrame(sheet, frame)) {
        return NULL;
    }

    atlas = sheet->frames[frame].atlas;
    if (atlas < 0 || atlas >= sheet->num_atlases || !sheet->atlases[atlas].texture) {
        SDL_SetError("Atlas %d of the sprite sheet is not loaded", atlas);
        return NULL;
    }

    return sheet->atlases[atlas].texture;
}

int SPRAT_GetFrameFromName(const SPRAT_Sheet *sheet, const char *name)
{
    int index = -1;
    int name_matches = 0;
    char escaped[ESCAPED_SIZE];
    int i;

    if (!sheet) {
        SDL_InvalidParamError("sheet");
        return -1;
    }
    if (!name) {
        SDL_InvalidParamError("name");
        return -1;
    }

    for (i = 0; i < sheet->num_frames; i++) {
        if (SDL_strcmp(sheet->frames[i].path, name) == 0) {
            return i;
        }
        if (SDL_strcmp(sheet->frames[i].name, name) == 0) {
            if (name_matches == 0) {
                index = i;
            }
            name_matches++;
        }
    }

    if (name_matches == 0) {
        SDL_SetError("Couldn't find frame %s", Escape(escaped, name));
    } else if (name_matches > 1) {
        SDL_SetError("Frame name %s is ambiguous, %d frames have it: use the sprite path instead", Escape(escaped, name), name_matches);
        index = -1;
    }

    return index;
}

const SPRAT_Marker *SPRAT_GetFrameMarker(const SPRAT_Sheet *sheet, int frame, const char *name)
{
    const SPRAT_Frame *f;
    char escaped_path[ESCAPED_SIZE], escaped_name[ESCAPED_SIZE];
    int i;

    if (!CheckFrame(sheet, frame)) {
        return NULL;
    }
    if (!name) {
        SDL_InvalidParamError("name");
        return NULL;
    }

    f = &sheet->frames[frame];
    for (i = 0; i < f->num_markers; i++) {
        if (SDL_strcmp(f->markers[i].name, name) == 0) {
            return &f->markers[i];
        }
    }

    SDL_SetError("Frame %s has no marker %s", Escape(escaped_path, f->path), Escape(escaped_name, name));
    return NULL;
}

const SPRAT_Marker *SPRAT_GetSpriteMarker(const SPRAT_Sprite *sprite, const char *name)
{
    if (!sprite) {
        SDL_InvalidParamError("sprite");
        return NULL;
    }
    return SPRAT_GetFrameMarker(sprite->sheet, sprite->frame, name);
}

/* A transform of original-image points: scaled, then flipped around the pivot, then rotated
   clockwise around it, with the pivot moved to (x, y) */
typedef struct Transform
{
    SDL_FPoint pivot;
    float x, y;
    float scale_x, scale_y;
    float cos_angle, sin_angle;
} Transform;

/* Get the cosine and sine of a clockwise angle in degrees. Quarter turns are exact, without
   trigonometry, so that a turned point keeps whole coordinates */
static void GetAngleCosSin(double angle, double *cos_angle, double *sin_angle)
{
    double turn = SDL_fmod(angle, 360.0);

    if (turn < 0.0) {
        turn += 360.0;
    }

    if (turn == 0.0) {
        *cos_angle = 1.0;
        *sin_angle = 0.0;
    } else if (turn == 90.0) {
        *cos_angle = 0.0;
        *sin_angle = 1.0;
    } else if (turn == 180.0) {
        *cos_angle = -1.0;
        *sin_angle = 0.0;
    } else if (turn == 270.0) {
        *cos_angle = 0.0;
        *sin_angle = -1.0;
    } else {
        double radians = angle * (SDL_PI_D / 180.0);
        *cos_angle = SDL_cos(radians);
        *sin_angle = SDL_sin(radians);
    }
}

static Transform MakeTransform(const SPRAT_Frame *frame, float x, float y, float scale_x, float scale_y, double angle, SDL_FlipMode flip)
{
    Transform t;
    double cos_angle, sin_angle;

    t.pivot = frame->pivot;
    t.x = x;
    t.y = y;
    t.scale_x = (flip & SDL_FLIP_HORIZONTAL) ? -scale_x : scale_x;
    t.scale_y = (flip & SDL_FLIP_VERTICAL) ? -scale_y : scale_y;

    GetAngleCosSin(angle, &cos_angle, &sin_angle);
    t.cos_angle = (float)cos_angle;
    t.sin_angle = (float)sin_angle;

    return t;
}

static SDL_FPoint TransformPoint(const Transform *t, float px, float py)
{
    SDL_FPoint result;
    float dx = (px - t->pivot.x) * t->scale_x;
    float dy = (py - t->pivot.y) * t->scale_y;

    result.x = t->x + dx * t->cos_angle - dy * t->sin_angle;
    result.y = t->y + dx * t->sin_angle + dy * t->cos_angle;

    return result;
}

/* Get the SDL_RenderTextureRotated() arguments that draw a stored atlas region upright, size
   wide and high, centered on center, then flipped and rotated clockwise around its center.
   A region is stored turned by the frame's rotation, so its rectangle is turned back by the
   opposite; and SDL flips the stored image before turning it, so a turned region swaps its flip
   axes */
static DrawCall GetRegionDrawCall(const SPRAT_Frame *frame, SDL_FRect source, SDL_FPoint center, SDL_FPoint size, double angle, SDL_FlipMode flip)
{
    bool turned = (frame->rotation != 0);
    float w = turned ? size.y : size.x;     /* the stored rectangle: a turned region's width is its upright height */
    float h = turned ? size.x : size.y;
    DrawCall call;

    call.source = source;
    call.dest.x = center.x - w / 2.0f;
    call.dest.y = center.y - h / 2.0f;
    call.dest.w = w;
    call.dest.h = h;
    call.angle = angle - frame->rotation;
    call.flip = flip;

    if (turned) {
        call.flip = (SDL_FlipMode)(((flip & SDL_FLIP_HORIZONTAL) ? SDL_FLIP_VERTICAL : 0) |
                                   ((flip & SDL_FLIP_VERTICAL) ? SDL_FLIP_HORIZONTAL : 0));
    }

    return call;
}

/* Get the SDL_RenderTextureRotated() arguments that draw a sprite. The sprite must have been
   checked */
static DrawCall GetSpriteDrawCall(const SPRAT_Sprite *sprite)
{
    const SPRAT_Frame *frame = &sprite->sheet->frames[sprite->frame];
    float scale_x = sprite->scale_x, scale_y = sprite->scale_y;
    SDL_FlipMode flip = sprite->flip;
    Transform t;
    SDL_FPoint center, size;

    /* A negative scale mirrors around the pivot, as a flip does */
    if (scale_x < 0.0f) {
        scale_x = -scale_x;
        flip = (SDL_FlipMode)(flip ^ SDL_FLIP_HORIZONTAL);
    }
    if (scale_y < 0.0f) {
        scale_y = -scale_y;
        flip = (SDL_FlipMode)(flip ^ SDL_FLIP_VERTICAL);
    }

    t = MakeTransform(frame, sprite->x, sprite->y, scale_x, scale_y, sprite->angle, flip);
    center = TransformPoint(&t, frame->content.x + frame->content.w / 2.0f, frame->content.y + frame->content.h / 2.0f);
    size.x = frame->content.w * scale_x;
    size.y = frame->content.h * scale_y;

    return GetRegionDrawCall(frame, frame->source, center, size, sprite->angle, flip);
}

/* Get the atlas region that stores the upright content region (u0, v0)-(u1, v1).
   Packed clockwise (90), content (u, v) is stored at (height - v, u); packed counter-clockwise
   (270), at (v, width - u) */
static SDL_FRect GetStoredRegion(const SPRAT_Frame *frame, float u0, float v0, float u1, float v1)
{
    SDL_FRect region;

    if (frame->rotation == 90) {
        region.x = frame->source.x + frame->content.h - v1;
        region.y = frame->source.y + u0;
        region.w = v1 - v0;
        region.h = u1 - u0;
    } else if (frame->rotation == 270) {
        region.x = frame->source.x + v0;
        region.y = frame->source.y + frame->content.w - u1;
        region.w = v1 - v0;
        region.h = u1 - u0;
    } else {
        region.x = frame->source.x + u0;
        region.y = frame->source.y + v0;
        region.w = u1 - u0;
        region.h = v1 - v0;
    }

    return region;
}

/* Get the edges of the three slices along one axis of the destination */
static void GetSliceEdges(float start, float length, float before, float after, float edges[4])
{
    /* Borders shrink proportionally when they don't fit */
    if (before + after > length) {
        float ratio = length / (before + after);
        before *= ratio;
        after *= ratio;
    }

    edges[0] = start;
    edges[1] = start + before;
    edges[2] = start + length - after;
    edges[3] = start + length;
}

/* Get the SDL_RenderTextureRotated() arguments that draw a frame as a nine-slice into dstrect.
   Slices are cut in original-image space, then each is clipped to the trimmed content and mapped
   to the atlas region, which may be stored turned */
static int Get9GridDrawCalls(const SPRAT_Frame *frame, float scale, const SDL_FRect *dstrect, DrawCall calls[9])
{
    const SPRAT_Slice *slice = &frame->slice;
    float source_x[4], source_y[4], dest_x[4], dest_y[4];
    float content_right = frame->content.x + frame->content.w;
    float content_bottom = frame->content.y + frame->content.h;
    int count = 0;
    int row, col;

    if (frame->w <= 0.0f || frame->h <= 0.0f || dstrect->w <= 0.0f || dstrect->h <= 0.0f) {
        return 0;
    }
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    source_x[0] = 0.0f;
    source_x[1] = slice->left;
    source_x[2] = frame->w - slice->right;
    source_x[3] = frame->w;
    source_y[0] = 0.0f;
    source_y[1] = slice->top;
    source_y[2] = frame->h - slice->bottom;
    source_y[3] = frame->h;

    GetSliceEdges(dstrect->x, dstrect->w, slice->left * scale, slice->right * scale, dest_x);
    GetSliceEdges(dstrect->y, dstrect->h, slice->top * scale, slice->bottom * scale, dest_y);

    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            float x0 = source_x[col], x1 = source_x[col + 1];
            float y0 = source_y[row], y1 = source_y[row + 1];
            float clip_x0, clip_x1, clip_y0, clip_y1, scale_x, scale_y;
            SDL_FRect part;
            SDL_FPoint center, size;

            if (x1 <= x0 || y1 <= y0) {
                continue;
            }
            if (dest_x[col + 1] <= dest_x[col] || dest_y[row + 1] <= dest_y[row]) {
                continue;
            }

            /* Clip the slice to the content left after trimming */
            clip_x0 = SDL_max(x0, frame->content.x);
            clip_x1 = SDL_min(x1, content_right);
            clip_y0 = SDL_max(y0, frame->content.y);
            clip_y1 = SDL_min(y1, content_bottom);

            if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) {
                continue;
            }

            scale_x = (dest_x[col + 1] - dest_x[col]) / (x1 - x0);
            scale_y = (dest_y[row + 1] - dest_y[row]) / (y1 - y0);
            part.x = dest_x[col] + (clip_x0 - x0) * scale_x;
            part.y = dest_y[row] + (clip_y0 - y0) * scale_y;
            part.w = (clip_x1 - clip_x0) * scale_x;
            part.h = (clip_y1 - clip_y0) * scale_y;

            center.x = part.x + part.w / 2.0f;
            center.y = part.y + part.h / 2.0f;
            size.x = part.w;
            size.y = part.h;

            calls[count++] = GetRegionDrawCall(frame,
                                               GetStoredRegion(frame, clip_x0 - frame->content.x, clip_y0 - frame->content.y,
                                                               clip_x1 - frame->content.x, clip_y1 - frame->content.y),
                                               center, size, 0.0, SDL_FLIP_NONE);
        }
    }

    return count;
}

/* Get where the top-left, top-right and bottom-left corners of a draw call's source land: the
   corners of its rectangle, flipped, then rotated around its center */
static void GetDrawCallCorners(const DrawCall *call, SDL_FPoint *origin, SDL_FPoint *right, SDL_FPoint *down)
{
    float cx = call->dest.x + call->dest.w / 2.0f;
    float cy = call->dest.y + call->dest.h / 2.0f;
    float left = (call->flip & SDL_FLIP_HORIZONTAL) ? call->dest.w / 2.0f : -call->dest.w / 2.0f;
    float top = (call->flip & SDL_FLIP_VERTICAL) ? call->dest.h / 2.0f : -call->dest.h / 2.0f;
    double cos_double, sin_double;
    float cos_angle, sin_angle;

    GetAngleCosSin(call->angle, &cos_double, &sin_double);
    cos_angle = (float)cos_double;
    sin_angle = (float)sin_double;

    origin->x = cx + left * cos_angle - top * sin_angle;
    origin->y = cy + left * sin_angle + top * cos_angle;
    right->x = cx - left * cos_angle - top * sin_angle;
    right->y = cy - left * sin_angle + top * cos_angle;
    down->x = cx + left * cos_angle + top * sin_angle;
    down->y = cy + left * sin_angle - top * cos_angle;
}

static bool IsSoftwareRenderer(SDL_Renderer *renderer)
{
    const char *name = SDL_GetRendererName(renderer);

    return name && SDL_strcmp(name, SDL_SOFTWARE_RENDERER) == 0;
}

static bool RenderDrawCall(SDL_Renderer *renderer, SDL_Texture *texture, const DrawCall *call, bool software)
{
    /* Upright and unflipped, the plain copy is the fastest path on every renderer */
    if (call->angle == 0.0 && call->flip == SDL_FLIP_NONE) {
        return SDL_RenderTexture(renderer, texture, &call->source, &call->dest);
    }

    /* The software renderer rounds the rectangle to whole pixels before rotating it, which shifts
       a turned frame with an odd side by half a pixel. Its corners are whole, so the same copy
       given by its corners lands where the hardware renderers put it */
    if (software) {
        SDL_FPoint origin, right, down;

        GetDrawCallCorners(call, &origin, &right, &down);
        return SDL_RenderTextureAffine(renderer, texture, &call->source, &origin, &right, &down);
    }

    return SDL_RenderTextureRotated(renderer, texture, &call->source, &call->dest, call->angle, NULL, call->flip);
}

/* ---- Marker overlaps ------------------------------------------------------ */

/* How far past touching, in the unit space of an ellipse, still counts as touching: rounding
   would otherwise decide whether a circle touching a shape overlaps it */
#define UNIT_TOLERANCE 1e-9

typedef struct DPoint
{
    double x, y;
} DPoint;

/* An affine map: (m[0] * x + m[1] * y + tx, m[2] * x + m[3] * y + ty) */
typedef struct Affine
{
    double m[4];
    double tx, ty;
} Affine;

/* A marker placed on the screen: a polygon, which is a point or a segment when it has one or two
   vertices, or an ellipse, the image of the unit disk by center + l */
typedef struct Shape
{
    bool ellipse;
    DPoint center;
    double l[4];
    int num_vertices;
    DPoint points[4];           /* the vertices of a point, a rectangle or a flattened ellipse */
    const SDL_FPoint *vertices; /* or the vertices of a polygon marker, placed by affine */
    Affine affine;
} Shape;

/* Where shapes are compared: the screen, or the unit space of an ellipse, where it is the
   unit disk. A screen point p is at inverse * (p - origin) */
typedef struct Space
{
    DPoint origin;
    double inverse[4];
} Space;

static const Space screen_space = { { 0.0, 0.0 }, { 1.0, 0.0, 0.0, 1.0 } };

static DPoint MapPoint(const Affine *a, double x, double y)
{
    DPoint p;

    p.x = a->m[0] * x + a->m[1] * y + a->tx;
    p.y = a->m[2] * x + a->m[3] * y + a->ty;
    return p;
}

static DPoint MultiplyPoint(const double m[4], double x, double y)
{
    DPoint p;

    p.x = m[0] * x + m[1] * y;
    p.y = m[2] * x + m[3] * y;
    return p;
}

static double Determinant(const double m[4])
{
    return m[0] * m[3] - m[1] * m[2];
}

static void Invert(const double m[4], double det, double inverse[4])
{
    inverse[0] = m[3] / det;
    inverse[1] = -m[1] / det;
    inverse[2] = -m[2] / det;
    inverse[3] = m[0] / det;
}

/* The map a sprite's original-image points are placed by, the same as MakeTransform()'s. The
   sprite must have been checked */
static Affine GetSpriteAffine(const SPRAT_Sprite *sprite)
{
    const SPRAT_Frame *frame = &sprite->sheet->frames[sprite->frame];
    double scale_x = (sprite->flip & SDL_FLIP_HORIZONTAL) ? -sprite->scale_x : sprite->scale_x;
    double scale_y = (sprite->flip & SDL_FLIP_VERTICAL) ? -sprite->scale_y : sprite->scale_y;
    double cos_angle, sin_angle;
    Affine a;

    GetAngleCosSin(sprite->angle, &cos_angle, &sin_angle);
    a.m[0] = scale_x * cos_angle;
    a.m[1] = -scale_y * sin_angle;
    a.m[2] = scale_x * sin_angle;
    a.m[3] = scale_y * cos_angle;
    a.tx = sprite->x - (a.m[0] * frame->pivot.x + a.m[1] * frame->pivot.y);
    a.ty = sprite->y - (a.m[2] * frame->pivot.x + a.m[3] * frame->pivot.y);
    return a;
}

/* Set a shape to the ellipse center + l, or to what it is flattened to by a radius or a scale of
   0. l's columns are where the frame's x and y axes go, scaled by the radius: a scale of 0 zeroes
   one, leaving the segment center +- the other, and a radius of 0 both, leaving a point */
static void SetEllipse(Shape *shape, DPoint center, const double l[4])
{
    int column = (l[0] != 0.0 || l[2] != 0.0) ? 0 : 1;

    shape->center = center;
    SDL_memcpy(shape->l, l, sizeof(shape->l));

    if (Determinant(l) != 0.0) {
        shape->ellipse = true;
    } else if (l[column] == 0.0 && l[column + 2] == 0.0) {
        shape->num_vertices = 1;
        shape->points[0] = center;
    } else {
        shape->num_vertices = 2;
        shape->points[0].x = center.x - l[column];
        shape->points[0].y = center.y - l[column + 2];
        shape->points[1].x = center.x + l[column];
        shape->points[1].y = center.y + l[column + 2];
    }
}

/* Place a sprite's marker on the screen, setting the error when an argument is not valid */
static bool GetMarkerShape(const SPRAT_Sprite *sprite, const SPRAT_Marker *marker, const char *sprite_param, const char *marker_param,
                           Shape *shape)
{
    char escaped[ESCAPED_SIZE];
    Affine a;

    if (!CheckSprite(sprite, sprite_param)) {
        return false;
    }
    if (!marker) {
        return SDL_InvalidParamError(marker_param);
    }

    SDL_zerop(shape);
    a = GetSpriteAffine(sprite);

    switch (marker->type) {
    case SPRAT_MARKERTYPE_POINT:
        shape->num_vertices = 1;
        shape->points[0] = MapPoint(&a, marker->position.x, marker->position.y);
        return true;

    case SPRAT_MARKERTYPE_RECTANGLE:
        shape->num_vertices = 4;
        shape->points[0] = MapPoint(&a, marker->position.x, marker->position.y);
        shape->points[1] = MapPoint(&a, (double)marker->position.x + marker->w, marker->position.y);
        shape->points[2] = MapPoint(&a, (double)marker->position.x + marker->w, (double)marker->position.y + marker->h);
        shape->points[3] = MapPoint(&a, marker->position.x, (double)marker->position.y + marker->h);
        return true;

    case SPRAT_MARKERTYPE_CIRCLE: {
        double l[4];
        int i;

        for (i = 0; i < 4; i++) {
            l[i] = a.m[i] * marker->radius;
        }
        SetEllipse(shape, MapPoint(&a, marker->position.x, marker->position.y), l);
        return true;
    }

    case SPRAT_MARKERTYPE_POLYGON:
        if (marker->num_vertices < 1 || !marker->vertices) {
            return SDL_SetError("Polygon marker %s has no vertices", Escape(escaped, marker->name ? marker->name : ""));
        }
        shape->num_vertices = marker->num_vertices;
        shape->vertices = marker->vertices;
        shape->affine = a;
        return true;

    default:
        return SDL_SetError("Marker %s has an unknown type %d", Escape(escaped, marker->name ? marker->name : ""), (int)marker->type);
    }
}

/* Get vertex i of a polygon shape, in a space */
static DPoint GetVertex(const Shape *shape, int i, const Space *space)
{
    DPoint p = shape->vertices ? MapPoint(&shape->affine, shape->vertices[i].x, shape->vertices[i].y) : shape->points[i];

    return MultiplyPoint(space->inverse, p.x - space->origin.x, p.y - space->origin.y);
}

/* The number of edges of a polygon shape: a point is one edge from itself to itself, and a
   segment the edge back and forth */
static int GetEdgeCount(const Shape *shape)
{
    return (shape->num_vertices < 3) ? 1 : shape->num_vertices;
}

static double Cross(DPoint o, DPoint a, DPoint b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/* Whether p, known to be on the line through a and b, lies between them */
static bool IsOnSegment(DPoint a, DPoint b, DPoint p)
{
    return SDL_min(a.x, b.x) <= p.x && p.x <= SDL_max(a.x, b.x) &&
           SDL_min(a.y, b.y) <= p.y && p.y <= SDL_max(a.y, b.y);
}

/* Whether segments p0-p1 and q0-q1 share a point, ends included; either may be a single point */
static bool SegmentsIntersect(DPoint p0, DPoint p1, DPoint q0, DPoint q1)
{
    double d0 = Cross(q0, q1, p0);
    double d1 = Cross(q0, q1, p1);
    double d2 = Cross(p0, p1, q0);
    double d3 = Cross(p0, p1, q1);

    if (((d0 > 0.0 && d1 < 0.0) || (d0 < 0.0 && d1 > 0.0)) &&
        ((d2 > 0.0 && d3 < 0.0) || (d2 < 0.0 && d3 > 0.0))) {
        return true;
    }

    return (d0 == 0.0 && IsOnSegment(q0, q1, p0)) || (d1 == 0.0 && IsOnSegment(q0, q1, p1)) ||
           (d2 == 0.0 && IsOnSegment(p0, p1, q0)) || (d3 == 0.0 && IsOnSegment(p0, p1, q1));
}

/* Whether p is inside a polygon shape of 3 vertices or more, by the even-odd rule; points on its
   edges are left to the edge tests */
static bool IsInsidePolygon(DPoint p, const Shape *shape, const Space *space)
{
    DPoint previous = GetVertex(shape, shape->num_vertices - 1, space);
    bool inside = false;
    int i;

    for (i = 0; i < shape->num_vertices; i++) {
        DPoint current = GetVertex(shape, i, space);

        if ((current.y > p.y) != (previous.y > p.y) &&
            p.x < (previous.x - current.x) * (p.y - current.y) / (previous.y - current.y) + current.x) {
            inside = !inside;
        }
        previous = current;
    }

    return inside;
}

/* Two polygons overlap when edges of both cross or touch, or else when one is inside the other */
static bool PolygonsOverlap(const Shape *a, const Shape *b)
{
    int i, j;

    for (i = 0; i < GetEdgeCount(a); i++) {
        DPoint a0 = GetVertex(a, i, &screen_space);
        DPoint a1 = GetVertex(a, (i + 1) % a->num_vertices, &screen_space);

        for (j = 0; j < GetEdgeCount(b); j++) {
            if (SegmentsIntersect(a0, a1, GetVertex(b, j, &screen_space), GetVertex(b, (j + 1) % b->num_vertices, &screen_space))) {
                return true;
            }
        }
    }

    return (b->num_vertices >= 3 && IsInsidePolygon(GetVertex(a, 0, &screen_space), b, &screen_space)) ||
           (a->num_vertices >= 3 && IsInsidePolygon(GetVertex(b, 0, &screen_space), a, &screen_space));
}

/* Get the unit space of an ellipse, where it is the unit disk */
static Space GetUnitSpace(const Shape *ellipse)
{
    Space space;

    space.origin = ellipse->center;
    Invert(ellipse->l, Determinant(ellipse->l), space.inverse);
    return space;
}

/* The squared distance from the origin to segment a-b */
static double GetSquaredDistanceToOrigin(DPoint a, DPoint b)
{
    double dx = b.x - a.x, dy = b.y - a.y;
    double length = dx * dx + dy * dy;
    double t = 0.0;

    if (length > 0.0) {
        t = SDL_clamp(-(a.x * dx + a.y * dy) / length, 0.0, 1.0);
    }
    a.x += t * dx;
    a.y += t * dy;
    return a.x * a.x + a.y * a.y;
}

/* An ellipse and a polygon overlap when, in the ellipse's unit space, an edge comes within 1 of
   the origin, or the origin is inside the polygon */
static bool EllipseOverlapsPolygon(const Shape *ellipse, const Shape *polygon)
{
    Space space = GetUnitSpace(ellipse);
    DPoint origin = { 0.0, 0.0 };
    int i;

    for (i = 0; i < GetEdgeCount(polygon); i++) {
        DPoint p0 = GetVertex(polygon, i, &space);
        DPoint p1 = GetVertex(polygon, (i + 1) % polygon->num_vertices, &space);

        if (GetSquaredDistanceToOrigin(p0, p1) <= 1.0 + UNIT_TOLERANCE) {
            return true;
        }
    }

    return polygon->num_vertices >= 3 && IsInsidePolygon(origin, polygon, &space);
}

/* Two ellipses overlap when, in the unit space of a, the ellipse c + l * u (|u| <= 1) that b
   becomes holds the origin or comes within 1 of it. Its nearest point is on its edge: where
   (l^T l + mu) u = -l^T c for the mu >= 0 that gives |u| = 1, found by bisection */
static bool EllipsesOverlap(const Shape *a, const Shape *b)
{
    Space space = GetUnitSpace(a);
    DPoint c = MultiplyPoint(space.inverse, b->center.x - a->center.x, b->center.y - a->center.y);
    double l[4], inverse[4];
    double p00, p01, p11, mean, half_difference, root, lambda0, lambda1, theta;
    double e0x, e0y, e1x, e1y, g0, g1, gx, gy, low, high, mu, ux, uy, length;
    DPoint u, nearest;
    int i;

    l[0] = space.inverse[0] * b->l[0] + space.inverse[1] * b->l[2];
    l[1] = space.inverse[0] * b->l[1] + space.inverse[1] * b->l[3];
    l[2] = space.inverse[2] * b->l[0] + space.inverse[3] * b->l[2];
    l[3] = space.inverse[2] * b->l[1] + space.inverse[3] * b->l[3];

    /* The origin inside b */
    Invert(l, Determinant(l), inverse);
    u = MultiplyPoint(inverse, -c.x, -c.y);
    if (u.x * u.x + u.y * u.y <= 1.0 + UNIT_TOLERANCE) {
        return true;
    }

    /* l^T l, its eigenvalues and eigenvectors, and l^T c along them */
    p00 = l[0] * l[0] + l[2] * l[2];
    p01 = l[0] * l[1] + l[2] * l[3];
    p11 = l[1] * l[1] + l[3] * l[3];
    mean = (p00 + p11) / 2.0;
    half_difference = (p00 - p11) / 2.0;
    root = SDL_sqrt(half_difference * half_difference + p01 * p01);
    lambda0 = mean + root;
    lambda1 = SDL_max(mean - root, 0.0);
    theta = SDL_atan2(2.0 * p01, p00 - p11) / 2.0;
    e0x = SDL_cos(theta);
    e0y = SDL_sin(theta);
    e1x = -e0y;
    e1y = e0x;
    gx = l[0] * c.x + l[2] * c.y;
    gy = l[1] * c.x + l[3] * c.y;
    g0 = e0x * gx + e0y * gy;
    g1 = e1x * gx + e1y * gy;

    /* |u(mu)| falls from over 1 at mu = 0, where u is the point found above, to at most 1 at |g| */
    low = 0.0;
    high = SDL_sqrt(gx * gx + gy * gy);
    for (i = 0; i < 200 && low < high; i++) {
        double middle = (low + high) / 2.0;
        double u0 = g0 / (lambda0 + middle);
        double u1 = (g1 == 0.0) ? 0.0 : g1 / (lambda1 + middle);

        if (middle == low || middle == high) {
            break;
        }
        if (u0 * u0 + u1 * u1 > 1.0) {
            low = middle;
        } else {
            high = middle;
        }
    }
    mu = high;

    /* The nearest point on b's edge, kept on it by normalizing u */
    ux = -(g0 / (lambda0 + mu)) * e0x - ((g1 == 0.0) ? 0.0 : g1 / (lambda1 + mu)) * e1x;
    uy = -(g0 / (lambda0 + mu)) * e0y - ((g1 == 0.0) ? 0.0 : g1 / (lambda1 + mu)) * e1y;
    length = SDL_sqrt(ux * ux + uy * uy);
    if (length > 0.0) {
        ux /= length;
        uy /= length;
    }
    nearest = MultiplyPoint(l, ux, uy);
    nearest.x += c.x;
    nearest.y += c.y;

    return nearest.x * nearest.x + nearest.y * nearest.y <= 1.0 + UNIT_TOLERANCE;
}

bool SPRAT_GetSpritePoint(const SPRAT_Sprite *sprite, const SDL_FPoint *point, SDL_FPoint *result)
{
    Affine a;
    DPoint p;

    if (!CheckSprite(sprite, "sprite")) {
        return false;
    }
    if (!point) {
        return SDL_InvalidParamError("point");
    }
    if (!result) {
        return SDL_InvalidParamError("result");
    }

    /* Placed like markers, so a marker's center is where its point would be */
    a = GetSpriteAffine(sprite);
    p = MapPoint(&a, point->x, point->y);
    result->x = (float)p.x;
    result->y = (float)p.y;
    return true;
}

/* Get the center of a polygon marker in original-image pixels: its centroid, or the middle of its
   ends when it has no area. Taken around the first vertex, which keeps the sums small */
static DPoint GetPolygonCenter(const SDL_FPoint *vertices, int num_vertices)
{
    double x0 = vertices[0].x, y0 = vertices[0].y;
    double min_x = x0, max_x = x0, min_y = y0, max_y = y0;
    double area = 0.0, cx = 0.0, cy = 0.0;
    DPoint center;
    int i;

    for (i = 0; i < num_vertices; i++) {
        double ax = vertices[i].x - x0, ay = vertices[i].y - y0;
        double bx = vertices[(i + 1) % num_vertices].x - x0, by = vertices[(i + 1) % num_vertices].y - y0;
        double cross = ax * by - bx * ay;

        area += cross;
        cx += (ax + bx) * cross;
        cy += (ay + by) * cross;
        min_x = SDL_min(min_x, vertices[i].x);
        max_x = SDL_max(max_x, vertices[i].x);
        min_y = SDL_min(min_y, vertices[i].y);
        max_y = SDL_max(max_y, vertices[i].y);
    }

    if (area != 0.0) {
        center.x = x0 + cx / (3.0 * area);
        center.y = y0 + cy / (3.0 * area);
    } else {
        /* A point, a segment, or vertices on a line: the middle of its ends */
        center.x = (min_x + max_x) / 2.0;
        center.y = (min_y + max_y) / 2.0;
    }
    return center;
}

bool SPRAT_GetMarkerCenter(const SPRAT_Sprite *sprite, const SPRAT_Marker *marker, SDL_FPoint *result)
{
    char escaped[ESCAPED_SIZE];
    DPoint center;
    Affine a;

    if (!CheckSprite(sprite, "sprite")) {
        return false;
    }
    if (!marker) {
        return SDL_InvalidParamError("marker");
    }
    if (!result) {
        return SDL_InvalidParamError("result");
    }

    switch (marker->type) {
    case SPRAT_MARKERTYPE_POINT:
    case SPRAT_MARKERTYPE_CIRCLE:
        center.x = marker->position.x;
        center.y = marker->position.y;
        break;
    case SPRAT_MARKERTYPE_RECTANGLE:
        center.x = marker->position.x + marker->w / 2.0;
        center.y = marker->position.y + marker->h / 2.0;
        break;
    case SPRAT_MARKERTYPE_POLYGON:
        if (marker->num_vertices < 1 || !marker->vertices) {
            return SDL_SetError("Polygon marker %s has no vertices", Escape(escaped, marker->name ? marker->name : ""));
        }
        center = GetPolygonCenter(marker->vertices, marker->num_vertices);
        break;
    default:
        return SDL_SetError("Marker %s has an unknown type %d", Escape(escaped, marker->name ? marker->name : ""), (int)marker->type);
    }

    /* Placed like SPRAT_GetSpritePoint() places points and the overlap tests place shapes */
    a = GetSpriteAffine(sprite);
    center = MapPoint(&a, center.x, center.y);
    result->x = (float)center.x;
    result->y = (float)center.y;
    return true;
}

/* How far a circle's outline may stray from the curve, in render pixels, and how many sides it
   may have at most */
#define OUTLINE_TOLERANCE 0.25
#define MAX_CIRCLE_SIDES 256

/* The number of sides that keep a circle of this radius on the screen within the tolerance: each
   side's middle is radius * (1 - cos(pi / sides)) inside the curve */
static int GetCircleSides(double radius)
{
    double sides;

    if (!(radius > OUTLINE_TOLERANCE)) {
        return 8;
    }
    sides = SDL_ceil(SDL_PI_D / SDL_acos(1.0 - OUTLINE_TOLERANCE / radius));
    return (int)SDL_clamp(sides, 8.0, (double)MAX_CIRCLE_SIDES);
}

/* Add a point of an outline, if it fits */
static void AddOutlinePoint(const Affine *a, double x, double y, SDL_FPoint *points, int max_points, int *count)
{
    if (*count < max_points) {
        DPoint p = MapPoint(a, x, y);
        points[*count].x = (float)p.x;
        points[*count].y = (float)p.y;
    }
    (*count)++;
}

int SPRAT_GetMarkerOutline(const SPRAT_Sprite *sprite, const SPRAT_Marker *marker, SDL_FPoint *points, int max_points)
{
    char escaped[ESCAPED_SIZE];
    double x0, y0;
    int count = 0, sides, i;
    Affine a;

    if (!CheckSprite(sprite, "sprite")) {
        return -1;
    }
    if (!marker) {
        SDL_InvalidParamError("marker");
        return -1;
    }
    if (max_points < 0 || (!points && max_points > 0)) {
        SDL_InvalidParamError("points");
        return -1;
    }

    a = GetSpriteAffine(sprite);
    x0 = marker->position.x;
    y0 = marker->position.y;

    switch (marker->type) {
    case SPRAT_MARKERTYPE_POINT:
        AddOutlinePoint(&a, x0, y0, points, max_points, &count);
        return count;

    case SPRAT_MARKERTYPE_RECTANGLE:
        AddOutlinePoint(&a, x0, y0, points, max_points, &count);
        AddOutlinePoint(&a, x0 + marker->w, y0, points, max_points, &count);
        AddOutlinePoint(&a, x0 + marker->w, y0 + marker->h, points, max_points, &count);
        AddOutlinePoint(&a, x0, y0 + marker->h, points, max_points, &count);
        AddOutlinePoint(&a, x0, y0, points, max_points, &count);
        return count;

    case SPRAT_MARKERTYPE_CIRCLE:
        /* Sized by the longest axis the scale stretches it to */
        sides = GetCircleSides(SDL_fabs(marker->radius) * SDL_max(SDL_fabs(sprite->scale_x), SDL_fabs(sprite->scale_y)));
        for (i = 0; i < sides; i++) {
            double angle = (2.0 * SDL_PI_D * i) / sides;
            AddOutlinePoint(&a, x0 + marker->radius * SDL_cos(angle), y0 + marker->radius * SDL_sin(angle), points, max_points, &count);
        }
        AddOutlinePoint(&a, x0 + marker->radius, y0, points, max_points, &count);
        return count;

    case SPRAT_MARKERTYPE_POLYGON:
        if (marker->num_vertices < 1 || !marker->vertices) {
            SDL_SetError("Polygon marker %s has no vertices", Escape(escaped, marker->name ? marker->name : ""));
            return -1;
        }
        if (marker->num_vertices > SDL_MAX_SINT32 - 1) {
            SDL_SetError("Polygon marker %s has too many vertices", Escape(escaped, marker->name ? marker->name : ""));
            return -1;
        }
        for (i = 0; i < marker->num_vertices; i++) {
            AddOutlinePoint(&a, marker->vertices[i].x, marker->vertices[i].y, points, max_points, &count);
        }
        if (marker->num_vertices > 1) {
            AddOutlinePoint(&a, marker->vertices[0].x, marker->vertices[0].y, points, max_points, &count);
        }
        return count;

    default:
        SDL_SetError("Marker %s has an unknown type %d", Escape(escaped, marker->name ? marker->name : ""), (int)marker->type);
        return -1;
    }
}

bool SPRAT_MarkersOverlap(const SPRAT_Sprite *sprite_a, const SPRAT_Marker *marker_a, const SPRAT_Sprite *sprite_b, const SPRAT_Marker *marker_b)
{
    Shape a, b;

    if (!GetMarkerShape(sprite_a, marker_a, "sprite_a", "marker_a", &a) ||
        !GetMarkerShape(sprite_b, marker_b, "sprite_b", "marker_b", &b)) {
        return false;
    }

    if (a.ellipse && b.ellipse) {
        return EllipsesOverlap(&a, &b);
    }
    if (a.ellipse) {
        return EllipseOverlapsPolygon(&a, &b);
    }
    if (b.ellipse) {
        return EllipseOverlapsPolygon(&b, &a);
    }
    return PolygonsOverlap(&a, &b);
}

bool SPRAT_RenderFrame(SDL_Renderer *renderer, const SPRAT_Sheet *sheet, int frame, float x, float y)
{
    SPRAT_Sprite sprite = { NULL, 0, 0.0f, 0.0f, 1.0f, 1.0f, 0.0, SDL_FLIP_NONE };

    sprite.sheet = sheet;
    sprite.frame = frame;
    sprite.x = x;
    sprite.y = y;
    return SPRAT_RenderSprite(renderer, &sprite);
}

bool SPRAT_RenderSprite(SDL_Renderer *renderer, const SPRAT_Sprite *sprite)
{
    SDL_Texture *texture;
    DrawCall call;

    if (!CheckSprite(sprite, "sprite")) {
        return false;
    }
    texture = GetFrameTexture(sprite->sheet, sprite->frame);
    if (!texture) {
        return false;
    }

    call = GetSpriteDrawCall(sprite);
    return RenderDrawCall(renderer, texture, &call, IsSoftwareRenderer(renderer));
}

bool SPRAT_RenderFrame9Grid(SDL_Renderer *renderer, const SPRAT_Sheet *sheet, int frame, float scale, const SDL_FRect *dstrect)
{
    SDL_Texture *texture = GetFrameTexture(sheet, frame);
    DrawCall calls[9];
    bool software;
    int count, i;

    if (!texture) {
        return false;
    }
    if (!dstrect) {
        return SDL_InvalidParamError("dstrect");
    }

    count = Get9GridDrawCalls(&sheet->frames[frame], scale, dstrect, calls);
    software = IsSoftwareRenderer(renderer);
    for (i = 0; i < count; i++) {
        if (!RenderDrawCall(renderer, texture, &calls[i], software)) {
            return false;
        }
    }

    return true;
}

const SPRAT_Animation *SPRAT_GetAnimationFromName(const SPRAT_Sheet *sheet, const char *name)
{
    char escaped[ESCAPED_SIZE];
    int i;

    if (!sheet) {
        SDL_InvalidParamError("sheet");
        return NULL;
    }
    if (!name) {
        SDL_InvalidParamError("name");
        return NULL;
    }

    for (i = 0; i < sheet->num_animations; i++) {
        if (SDL_strcmp(sheet->animations[i].name, name) == 0) {
            return &sheet->animations[i];
        }
    }

    SDL_SetError("Couldn't find animation %s", Escape(escaped, name));
    return NULL;
}

/* Get the number of frames shown in one pass of a player's animation, 0 for none or one without
   frames. Ping-pong walks the frames back without repeating the last: 0 1 2 1
   repeats, and played once, 0 1 2 1 0 stops on the first frame. In double, so no frame count
   overflows it */
static double GetAnimationStepCount(const SPRAT_AnimationPlayer *player)
{
    const SPRAT_Animation *animation = player->animation;
    double count;

    if (!animation || animation->num_frames <= 0 || animation->fps <= 0) {
        return 0.0;
    }

    count = animation->num_frames;
    if ((player->loop_mode & SPRAT_LOOPMODE_PINGPONG) && count > 1.0) {
        count = 2.0 * count - ((player->loop_mode & SPRAT_LOOPMODE_ONCE) ? 1.0 : 2.0);
    }
    return count;
}

/* Get the duration in seconds of one pass of a player's animation, 0 for none or one without frames */
static double GetAnimationDuration(const SPRAT_AnimationPlayer *player)
{
    double count = GetAnimationStepCount(player);

    if (count <= 0.0) {
        return 0.0;
    }

    return count / player->animation->fps;
}

static bool IsFinite(double x)
{
    return !SDL_isinf(x) && !SDL_isnan(x);
}

/* Set the animation a player plays, with its loop mode */
static void SetPlayerAnimation(SPRAT_AnimationPlayer *player, const SPRAT_Animation *animation)
{
    player->animation = animation;
    player->loop_mode = animation ? animation->loop_mode : SPRAT_LOOPMODE_REPEAT;
}

void SPRAT_InitAnimationPlayer(SPRAT_AnimationPlayer *player, const SPRAT_Animation *animation)
{
    if (!player) {
        return;
    }

    SDL_zerop(player);
    SetPlayerAnimation(player, animation);
    player->speed = 1.0f;
    player->playing = true;
}

void SPRAT_SetAnimation(SPRAT_AnimationPlayer *player, const SPRAT_Animation *animation)
{
    /* The one played is left as it is, so it can be set every update */
    if (!player || player->animation == animation) {
        return;
    }

    SetPlayerAnimation(player, animation);
    SPRAT_PlayAnimation(player);
}

void SPRAT_UpdateAnimation(SPRAT_AnimationPlayer *player, float seconds)
{
    double duration, time;

    if (!player || !player->playing) {
        return;
    }

    duration = GetAnimationDuration(player);
    if (duration <= 0.0) {
        return;
    }

    time = (double)player->time + (double)seconds * player->speed;
    if (!IsFinite(time)) {
        return;     /* NaN or infinite time, step or speed: keep the last good time */
    }

    if (!(player->loop_mode & SPRAT_LOOPMODE_ONCE)) {
        /* Wrapping keeps the time precise however long the animation plays */
        time = SDL_fmod(time, duration);
        if (time < 0.0) {
            time += duration;
        }
    } else {
        if (time > duration) {
            time = duration;
        } else if (time < 0.0) {
            time = 0.0;
        }

        if ((player->speed > 0.0f && time >= duration) || (player->speed < 0.0f && time <= 0.0)) {
            player->playing = false;
        }
    }

    player->time = (float)time;
}

void SPRAT_PlayAnimation(SPRAT_AnimationPlayer *player)
{
    if (!player) {
        return;
    }

    player->time = (player->speed < 0.0f) ? (float)GetAnimationDuration(player) : 0.0f;
    player->playing = true;
}

void SPRAT_StopAnimation(SPRAT_AnimationPlayer *player)
{
    if (!player) {
        return;
    }

    SPRAT_PlayAnimation(player);
    player->playing = false;
}

void SPRAT_PauseAnimation(SPRAT_AnimationPlayer *player)
{
    if (player) {
        player->playing = false;
    }
}

void SPRAT_ResumeAnimation(SPRAT_AnimationPlayer *player)
{
    if (player) {
        player->playing = true;
    }
}

bool SPRAT_IsAnimationFinished(const SPRAT_AnimationPlayer *player)
{
    double duration;

    if (!player || !(player->loop_mode & SPRAT_LOOPMODE_ONCE)) {
        return false;
    }

    duration = GetAnimationDuration(player);
    if (duration <= 0.0) {
        return false;
    }

    return (player->speed < 0.0f) ? (player->time <= 0.0f) : (player->time >= (float)duration);
}

int SPRAT_GetAnimationFrame(const SPRAT_AnimationPlayer *player)
{
    const SPRAT_Animation *anim;
    double count, position, step;

    if (!player || !player->animation) {
        return -1;
    }

    anim = player->animation;
    count = GetAnimationStepCount(player);
    if (count <= 0.0 || !anim->frames) {
        return -1;
    }

    position = (double)player->time * anim->fps;
    step = (player->speed < 0.0f) ? SDL_ceil(position) - 1.0 : SDL_floor(position);
    if (!IsFinite(step)) {
        return -1;  /* NaN or infinite time: (int)step would be undefined */
    }

    if (!(player->loop_mode & SPRAT_LOOPMODE_ONCE)) {
        /* SDL_fmod() keeps the step exact and in range for any time, negative included */
        step = SDL_fmod(step, count);
        if (step < 0.0) {
            step += count;
        }
    } else if (step < 0.0) {
        step = 0.0;
    } else if (step > count - 1.0) {
        step = count - 1.0;
    }

    if (step >= anim->num_frames) {
        step = 2.0 * (anim->num_frames - 1) - step;     /* On the way back */
    }

    return anim->frames[(int)step];
}

void SPRAT_SetSpriteAnimationFrame(SPRAT_Sprite *sprite, const SPRAT_AnimationPlayer *player)
{
    if (!sprite) {
        return;
    }

    sprite->frame = SPRAT_GetAnimationFrame(player);
    sprite->flip = (player && player->animation) ? player->animation->flip : SDL_FLIP_NONE;
}

void SPRAT_UpdateSpriteAnimation(SPRAT_Sprite *sprite, SPRAT_AnimationPlayer *player, float seconds)
{
    if (!player) {
        return;
    }

    SPRAT_UpdateAnimation(player, seconds);
    SPRAT_SetSpriteAnimationFrame(sprite, player);
}

/* Make the sprite SPRAT_RenderAnimationRotated() renders: the frame shown now, mirrored by flip
   combined with the animation's flip */
static SPRAT_Sprite MakeAnimationSprite(const SPRAT_Sheet *sheet, const SPRAT_AnimationPlayer *player,
                                        float x, float y, float scale_x, float scale_y, double angle, SDL_FlipMode flip)
{
    SPRAT_Sprite sprite;

    sprite.sheet = sheet;
    SPRAT_SetSpriteAnimationFrame(&sprite, player);
    sprite.x = x;
    sprite.y = y;
    sprite.scale_x = scale_x;
    sprite.scale_y = scale_y;
    sprite.angle = angle;
    sprite.flip = (SDL_FlipMode)(sprite.flip ^ flip);
    return sprite;
}

bool SPRAT_RenderAnimation(SDL_Renderer *renderer, const SPRAT_Sheet *sheet, const SPRAT_AnimationPlayer *player, float x, float y)
{
    return SPRAT_RenderAnimationRotated(renderer, sheet, player, x, y, 1.0f, 1.0f, 0.0, SDL_FLIP_NONE);
}

bool SPRAT_RenderAnimationRotated(SDL_Renderer *renderer, const SPRAT_Sheet *sheet, const SPRAT_AnimationPlayer *player,
                                  float x, float y, float scale_x, float scale_y, double angle, SDL_FlipMode flip)
{
    SPRAT_Sprite sprite;

    if (!player) {
        return SDL_InvalidParamError("player");
    }

    sprite = MakeAnimationSprite(sheet, player, x, y, scale_x, scale_y, angle, flip);
    if (sprite.frame < 0) {
        return true;    /* Nothing to show */
    }
    return SPRAT_RenderSprite(renderer, &sprite);
}
