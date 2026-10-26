/*
  sprat-sdl: load and render sprat-cli sprite sheets with SDL3
  Copyright (C) 2026 Pedro Amaral Couto

  MIT License. See LICENSE.
*/

/**
 * # CategorySDLSprat
 *
 * Load sprite sheets made by [sprat-cli](https://github.com/pedroac/sprat-cli)
 * and render them with SDL_Renderer.
 *
 * A sheet is the metadata written by `spratconvert --transform raw`, parsed by
 * [sprat-lib](https://github.com/pedroac/sprat-lib), plus its atlas textures.
 * Frames are rendered the way they were authored: placed by their pivot, with
 * trimming and packing rotation undone, optionally scaled, rotated, flipped,
 * animated or stretched as nine-slices.
 *
 * ```c
 * SPRAT_Sheet *sheet = SPRAT_LoadSheet(renderer, "hero.sprat");
 * SPRAT_AnimationPlayer run;
 * SPRAT_InitAnimationPlayer(&run, SPRAT_GetAnimationFromName(sheet, "run"));
 * ...
 * SPRAT_UpdateAnimation(&run, seconds);
 * SPRAT_RenderAnimation(renderer, sheet, &run, 400.0f, 300.0f);
 * ...
 * SPRAT_DestroySheet(sheet);
 * ```
 *
 * Frame geometry (pivot, content, size, slice, markers) is in original-image
 * pixels, scaled to atlas pixels, with the origin at the top-left and Y
 * pointing down. Angles are in degrees, clockwise, as in
 * SDL_RenderTextureRotated().
 *
 * Frames keep the order of the sprites in the sheet, and animations the order
 * of the sheet's animations, so indexes from sprat-lib's sprat_sheet_bind()
 * index `sheet->frames` and `sheet->animations` as they are.
 */

#ifndef SDL_SPRAT_H_
#define SDL_SPRAT_H_

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_version.h>

#include <SDL3/SDL_begin_code.h>

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * The current major version of sprat-sdl headers.
 *
 * If this were sprat-sdl version 3.2.1, this value would be 3.
 *
 * \since This macro is available since sprat-sdl 0.1.0.
 */
#define SDL_SPRAT_MAJOR_VERSION   0

/**
 * The current minor version of sprat-sdl headers.
 *
 * If this were sprat-sdl version 3.2.1, this value would be 2.
 *
 * \since This macro is available since sprat-sdl 0.1.0.
 */
#define SDL_SPRAT_MINOR_VERSION   1

/**
 * The current micro (or patchlevel) version of sprat-sdl headers.
 *
 * If this were sprat-sdl version 3.2.1, this value would be 1.
 *
 * \since This macro is available since sprat-sdl 0.1.0.
 */
#define SDL_SPRAT_MICRO_VERSION   0

/**
 * This is the version number macro for the current sprat-sdl version.
 *
 * \since This macro is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_Version
 */
#define SDL_SPRAT_VERSION \
    SDL_VERSIONNUM(SDL_SPRAT_MAJOR_VERSION, SDL_SPRAT_MINOR_VERSION, SDL_SPRAT_MICRO_VERSION)

/**
 * This macro will evaluate to true if compiled with sprat-sdl at least X.Y.Z.
 *
 * \since This macro is available since sprat-sdl 0.1.0.
 */
#define SDL_SPRAT_VERSION_ATLEAST(X, Y, Z) \
    ((SDL_SPRAT_MAJOR_VERSION >= X) && \
     (SDL_SPRAT_MAJOR_VERSION > X || SDL_SPRAT_MINOR_VERSION >= Y) && \
     (SDL_SPRAT_MAJOR_VERSION > X || SDL_SPRAT_MINOR_VERSION > Y || SDL_SPRAT_MICRO_VERSION >= Z))

/**
 * This function gets the version of the dynamically linked sprat-sdl library.
 *
 * \returns sprat-sdl version.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 */
extern SDL_DECLSPEC int SDLCALL SPRAT_Version(void);

/**
 * The shape of a marker.
 *
 * \since This enum is available since sprat-sdl 0.1.0.
 */
typedef enum SPRAT_MarkerType
{
    SPRAT_MARKERTYPE_POINT,     /**< position */
    SPRAT_MARKERTYPE_CIRCLE,    /**< position (the center) and radius */
    SPRAT_MARKERTYPE_RECTANGLE, /**< position (the top-left corner), w and h */
    SPRAT_MARKERTYPE_POLYGON    /**< vertices, at least one (position holds the first) */
} SPRAT_MarkerType;

/**
 * How the stretchable regions of a nine-slice are filled, per axis.
 *
 * SPRAT_RenderFrame9Grid() renders every mode as SPRAT_SLICEMODE_STRETCH; the
 * mode says what the artist chose, for code that fills the middle itself.
 *
 * \since This enum is available since sprat-sdl 0.1.0.
 */
typedef enum SPRAT_SliceMode
{
    SPRAT_SLICEMODE_STRETCH,    /**< stretched to fit */
    SPRAT_SLICEMODE_REPEAT,     /**< tiled */
    SPRAT_SLICEMODE_MIRROR,     /**< tiled, every other tile mirrored */
    SPRAT_SLICEMODE_FIXED       /**< kept at its size */
} SPRAT_SliceMode;

/**
 * A named shape attached to a frame: its pivot, a hitbox, a hand, a spawn
 * point...
 *
 * Coordinates are in the frame's original-image pixels: a marker means
 * something once placed with a sprite of its frame. SPRAT_GetMarkerCenter()
 * tells where it is, SPRAT_MarkersOverlap() whether it overlaps another, and
 * SPRAT_GetMarkerOutline() gives its outline to draw.
 *
 * \since This struct is available since sprat-sdl 0.1.0.
 */
typedef struct SPRAT_Marker
{
    const char *name;               /**< the marker's name, UTF-8 */
    SPRAT_MarkerType type;          /**< the marker's shape */
    SDL_FPoint position;            /**< point, circle center, rectangle top-left or first polygon vertex */
    float radius;                   /**< circle radius */
    float w;                        /**< rectangle width */
    float h;                        /**< rectangle height */
    const SDL_FPoint *vertices;     /**< polygon vertices, NULL for other shapes */
    int num_vertices;               /**< number of polygon vertices */
} SPRAT_Marker;

/**
 * The nine-slice insets of a frame, in original-image pixels.
 *
 * All insets are 0 when the frame has no slice.
 *
 * \since This struct is available since sprat-sdl 0.1.0.
 */
typedef struct SPRAT_Slice
{
    float left;                     /**< width of the left column */
    float top;                      /**< height of the top row */
    float right;                    /**< width of the right column */
    float bottom;                   /**< height of the bottom row */
    SPRAT_SliceMode horizontal_mode; /**< how the middle column is filled */
    SPRAT_SliceMode vertical_mode;  /**< how the middle row is filled */
} SPRAT_Slice;

/**
 * One sprite of a sheet, as packed in its atlas.
 *
 * \since This struct is available since sprat-sdl 0.1.0.
 */
typedef struct SPRAT_Frame
{
    const char *name;               /**< the sprite's file basename without extension; names may repeat */
    const char *path;               /**< the sprite's source path, unique in the sheet */
    int atlas;                      /**< index into the sheet's atlases */
    SDL_FRect source;               /**< region in the atlas texture, as stored (turned when rotation != 0) */
    int rotation;                   /**< clockwise degrees the image was turned when packed: 0, 90 or 270 */
    float w;                        /**< original image width, trimmed margins included */
    float h;                        /**< original image height, trimmed margins included */
    SDL_FRect content;              /**< trimmed region inside the original image, upright */
    SDL_FPoint pivot;               /**< pivot inside the original image (0, 0 when none was set) */
    SPRAT_Slice slice;              /**< nine-slice insets */
    const SPRAT_Marker *markers;    /**< the frame's markers, NULL when it has none */
    int num_markers;                /**< number of markers */
} SPRAT_Frame;

/**
 * A sequence of frames played at a fixed rate.
 *
 * An alias is already resolved: `run-left`, an alias of `run` with `flip h`,
 * has `run`'s frames (the same array) and a flip of SDL_FLIP_HORIZONTAL.
 *
 * \since This struct is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_GetAnimationFromName
 * \sa SPRAT_InitAnimationPlayer
 */
typedef struct SPRAT_Animation
{
    const char *name;               /**< the animation's name, UTF-8 */
    int fps;                        /**< frames per second, 1 to 100 */
    const int *frames;              /**< indexes into the sheet's frames, in play order */
    int num_frames;                 /**< number of frame indexes, may be 0 */
    SDL_FlipMode flip;              /**< the flip the animation is played with */
} SPRAT_Animation;

/**
 * An atlas image of a sheet.
 *
 * \since This struct is available since sprat-sdl 0.1.0.
 */
typedef struct SPRAT_Atlas
{
    const char *path;               /**< the image path as the sheet wrote it, usually relative to the sheet's folder */
    int w;                          /**< the image width the sheet was packed for */
    int h;                          /**< the image height the sheet was packed for */
    SDL_Texture *texture;           /**< the atlas texture, NULL when not loaded; the sheet owns it */
} SPRAT_Atlas;

/**
 * A sprite sheet: atlas textures, frames and animations.
 *
 * Everything in a sheet is read-only, except the atlas textures: to load an
 * atlas yourself, assign `sheet->atlases[i].texture` (destroying the one
 * there, if any) and the sheet will destroy yours with it.
 *
 * Every array and string of a sheet stays valid until SPRAT_DestroySheet().
 *
 * \since This struct is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_LoadSheet
 * \sa SPRAT_DestroySheet
 */
typedef struct SPRAT_Sheet
{
    int num_atlases;                /**< number of atlases */
    SPRAT_Atlas *atlases;           /**< the atlases, NULL when there are none */
    int num_frames;                 /**< number of frames */
    const SPRAT_Frame *frames;      /**< the frames, in sheet order, NULL when there are none */
    int num_animations;             /**< number of animations */
    const SPRAT_Animation *animations; /**< the animations, in sheet order, NULL when there are none */
    float scale;                    /**< the scale the art was packed at: render at 1.0f / scale for its original size */
} SPRAT_Sheet;

/**
 * The playback state of an animation.
 *
 * Keep one per animated thing and update it every frame. A player allocates
 * nothing, so copy it or drop it freely. Its fields are yours to read and
 * set, `time` included.
 *
 * \since This struct is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_InitAnimationPlayer
 * \sa SPRAT_UpdateAnimation
 */
typedef struct SPRAT_AnimationPlayer
{
    const SPRAT_Animation *animation; /**< the animation played, NULL for none */
    float time;                     /**< seconds into the animation, from 0 to its duration (num_frames / fps) */
    float speed;                    /**< playback rate: 1.0f plays at the animation's fps, negative plays in reverse */
    bool playing;                   /**< true when SPRAT_UpdateAnimation() advances the time */
    bool loop;                      /**< true to wrap around at the end, false to stop on the last frame */
} SPRAT_AnimationPlayer;

/**
 * A frame placed on the screen: a sheet's frame, where its pivot is, and how
 * it is scaled, mirrored and rotated around the pivot.
 *
 * A sprite is not an object of the library: it is a plain struct, owned by the
 * program, that SPRAT_RenderSprite() renders and the marker functions place
 * markers with, so what is drawn and what collides always agree. Keep a
 * sprite as part of the program's state, and change its fields directly, such
 * as `frame` from an animation player's SPRAT_GetAnimationFrame() each update,
 * and `flip` combined with the animation's, as `(SDL_FlipMode)(flip ^
 * player->animation->flip)`.
 *
 * The frame is scaled, then mirrored around its pivot, then rotated clockwise
 * around it by `angle` degrees. `scale_x` and `scale_y` are not the sheet's
 * scale: frame geometry is in atlas pixels, so 1.0f renders atlas pixels one
 * to one. For a sheet packed at `--scale 0.5`, use `1.0f / sheet->scale` to
 * show the art at its original size. A scale of 0.0f shrinks the frame to
 * nothing, so set both scales: a sprite initialized as `{ sheet, frame, x, y }`
 * is not visible.
 *
 * The sheet's records call each image a "sprite"; here, a sprite is one of
 * them placed: `sheet->frames[frame]`, put on the screen.
 *
 * \since This struct is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_RenderSprite
 * \sa SPRAT_GetSpriteMarker
 * \sa SPRAT_GetSpritePoint
 * \sa SPRAT_GetMarkerCenter
 * \sa SPRAT_GetMarkerOutline
 * \sa SPRAT_MarkersOverlap
 */
typedef struct SPRAT_Sprite
{
    const SPRAT_Sheet *sheet;       /**< the sheet the frame belongs to */
    int frame;                      /**< the index of the frame in `sheet->frames` */
    float x;                        /**< the x coordinate of the frame's pivot */
    float y;                        /**< the y coordinate of the frame's pivot */
    float scale_x;                  /**< the horizontal scale, negative to mirror */
    float scale_y;                  /**< the vertical scale, negative to mirror */
    double angle;                   /**< the clockwise rotation around the pivot, in degrees */
    SDL_FlipMode flip;              /**< the mirroring around the pivot */
} SPRAT_Sprite;


/**
 * Load a sprite sheet from a file, and its atlases from the file's folder.
 *
 * Atlas paths are joined with the sheet's folder, unless they are absolute.
 * Atlases are loaded with SDL_LoadSurface(), which reads BMP and PNG. Pass a
 * NULL renderer to load only the metadata, and load the atlas textures
 * yourself (with SDL_image, for other formats) into `sheet->atlases[i].texture`.
 *
 * The atlas paths are used as written, so a sheet from an untrusted source
 * could name any file. Check such a sheet with sprat-lib's
 * sprat_path_is_safe() first, or load it without a renderer and check
 * `sheet->atlases[i].path` before loading each atlas.
 *
 * \param renderer the rendering context to create the atlas textures with,
 *                 or NULL to load no textures.
 * \param file the path to the `.sprat` file, UTF-8.
 * \returns a new sheet, or NULL on failure; call SDL_GetError() for more
 *          information. A sheet whose atlas can't be loaded, or is not the
 *          size the sheet was packed for, fails to load.
 *
 * \threadsafety With a renderer, this function should only be called on the
 *               main thread. Without one, it is safe to call from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_LoadSheet_IO
 * \sa SPRAT_DestroySheet
 */
extern SDL_DECLSPEC SPRAT_Sheet * SDLCALL SPRAT_LoadSheet(SDL_Renderer *renderer, const char *file);

/**
 * Load a sprite sheet from a data stream, and its atlases from a folder.
 *
 * Works as SPRAT_LoadSheet(), but reads the metadata from `src`.
 *
 * \param renderer the rendering context to create the atlas textures with,
 *                 or NULL to load no textures.
 * \param src the data stream with the `.sprat` metadata.
 * \param closeio if true, calls SDL_CloseIO() on `src` before returning, even
 *                in the case of an error.
 * \param atlas_dir the folder that relative atlas paths are joined with, or
 *                  NULL to use them as written.
 * \returns a new sheet, or NULL on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety With a renderer, this function should only be called on the
 *               main thread. Without one, it is safe to call from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_LoadSheet
 * \sa SPRAT_DestroySheet
 */
extern SDL_DECLSPEC SPRAT_Sheet * SDLCALL SPRAT_LoadSheet_IO(SDL_Renderer *renderer, SDL_IOStream *src, bool closeio, const char *atlas_dir);

/**
 * Destroy a sprite sheet and its atlas textures.
 *
 * Destroy sheets before the renderer their textures belong to.
 *
 * \param sheet the sheet to destroy, or NULL.
 *
 * \threadsafety This function should only be called on the main thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_LoadSheet
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_DestroySheet(SPRAT_Sheet *sheet);

/**
 * Get the index of a frame from its sprite path, or from its name when it is
 * unique.
 *
 * A frame's name is its file's basename and can repeat across folders, while
 * its path is unique. The path is matched first, then a name that only one
 * frame has. Look names up once, after loading, and keep the indexes.
 *
 * \param sheet the sheet.
 * \param name the sprite path, or a name only one frame has.
 * \returns the index into `sheet->frames`, or -1 when there is no such frame
 *          or the name is ambiguous; call SDL_GetError() for more information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 */
extern SDL_DECLSPEC int SDLCALL SPRAT_GetFrameFromName(const SPRAT_Sheet *sheet, const char *name);

/**
 * Get a marker of a frame by name.
 *
 * \param sheet the sheet.
 * \param frame the index of the frame.
 * \param name the marker's name.
 * \returns the marker, or NULL when the frame has none with that name; call
 *          SDL_GetError() for more information. The marker is valid until
 *          SPRAT_DestroySheet().
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_GetSpriteMarker
 */
extern SDL_DECLSPEC const SPRAT_Marker * SDLCALL SPRAT_GetFrameMarker(const SPRAT_Sheet *sheet, int frame, const char *name);

/**
 * Get a marker of a sprite's frame by name.
 *
 * Works as SPRAT_GetFrameMarker() with the sprite's sheet and frame. Look the
 * marker up again when the sprite's frame changes, such as each update of an
 * animated sprite: each frame has its own markers.
 *
 * \param sprite the sprite.
 * \param name the marker's name.
 * \returns the marker, or NULL when the frame has none with that name; call
 *          SDL_GetError() for more information. The marker is valid until
 *          SPRAT_DestroySheet().
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_GetFrameMarker
 * \sa SPRAT_GetMarkerCenter
 * \sa SPRAT_MarkersOverlap
 */
extern SDL_DECLSPEC const SPRAT_Marker * SDLCALL SPRAT_GetSpriteMarker(const SPRAT_Sprite *sprite, const char *name);

/**
 * Get where a point of a sprite's original image lands on the screen.
 *
 * The point is placed as SPRAT_RenderSprite() places the frame. For where a
 * marker is, use SPRAT_GetMarkerCenter(), and SPRAT_GetMarkerOutline() to
 * draw it.
 *
 * \param sprite the sprite.
 * \param point the point, in the frame's original-image pixels.
 * \param result filled in with the point in render coordinates.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_GetMarkerCenter
 * \sa SPRAT_RenderSprite
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_GetSpritePoint(const SPRAT_Sprite *sprite, const SDL_FPoint *point, SDL_FPoint *result);

/**
 * Get where a marker of a sprite is: a point marker's point, or the center
 * of another shape.
 *
 * The marker is placed as SPRAT_RenderSprite() places the sprite's frame. The
 * center is a circle's center, the middle of a rectangle, and a polygon's
 * centroid (its center of mass, which is outside a concave polygon whose middle
 * is cut away); a polygon of two vertices is a segment, centered on its middle,
 * and one whose vertices are all on a line is centered on the middle of its
 * ends. Use it to attach an item to a "hand", to spawn something at a "muzzle",
 * or to aim at a hitbox.
 *
 * \param sprite the sprite.
 * \param marker the marker, in the frame's original-image pixels.
 * \param result filled in with the marker's point or center, in render
 *               coordinates.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_GetSpriteMarker
 * \sa SPRAT_MarkersOverlap
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_GetMarkerCenter(const SPRAT_Sprite *sprite, const SPRAT_Marker *marker, SDL_FPoint *result);

/**
 * Get the outline of a marker of a sprite, to draw it.
 *
 * The marker is placed as SPRAT_RenderSprite() places the sprite's frame.
 * The outline is closed, ending on the point it starts
 * from, so SDL_RenderLines(renderer, points, count) draws the whole shape: a
 * rectangle's 4 corners, from its top-left one, and its first again; a
 * polygon's vertices and its first again; or points around a circle, turned
 * and scaled with the frame, as many as keep the outline within a quarter of
 * a pixel of the curve (up to 256, then the first again). A point marker's
 * outline, and a polygon's of one vertex, is that one point.
 *
 * Like snprintf(), it returns how many points the outline has, and fills
 * `points` with as many of them as fit. Pass NULL and 0 to only count them;
 * a circle's count depends on its size on the screen.
 *
 * \param sprite the sprite.
 * \param marker the marker, in the frame's original-image pixels.
 * \param points filled in with the outline's points, in render coordinates;
 *               may be NULL when max_points is 0.
 * \param max_points the number of points `points` can hold.
 * \returns the number of points of the outline, which may be more than
 *          max_points, or -1 on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_GetSpriteMarker
 * \sa SPRAT_GetMarkerCenter
 */
extern SDL_DECLSPEC int SDLCALL SPRAT_GetMarkerOutline(const SPRAT_Sprite *sprite, const SPRAT_Marker *marker, SDL_FPoint *points, int max_points);

/**
 * Check whether markers of two sprites overlap, such as two hitboxes.
 *
 * Each marker is placed as SPRAT_RenderSprite() places its sprite's frame,
 * and the shapes are compared exactly: a rectangle turns
 * with its frame, and a circle scaled unevenly is an ellipse. Shapes that
 * only touch overlap, and so does a shape inside the other. A point marker
 * overlaps the shapes it lies in, and a polygon may be concave. A shape
 * scaled to 0 is flattened to a segment or a point, which still overlaps
 * what it touches.
 *
 * The sprites may come from different sheets, and a marker need not belong to
 * its sprite's frame: any SPRAT_Marker in the frame's original-image pixels
 * works, such as an attack range made by the program.
 *
 * \param sprite_a the first sprite.
 * \param marker_a the first sprite's marker.
 * \param sprite_b the second sprite, which may be sprite_a.
 * \param marker_b the second sprite's marker.
 * \returns true if the markers overlap, false if they don't or on failure;
 *          call SDL_GetError() for more information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_GetSpriteMarker
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_MarkersOverlap(const SPRAT_Sprite *sprite_a, const SPRAT_Marker *marker_a, const SPRAT_Sprite *sprite_b, const SPRAT_Marker *marker_b);

/**
 * Render a frame with its pivot at a point.
 *
 * Trimmed margins are left out, and frames packed turned are rendered
 * upright. Use the atlas texture's color and alpha mod, blend mode and scale
 * mode to change how it is drawn.
 *
 * \param renderer the renderer to draw with.
 * \param sheet the sheet.
 * \param frame the index of the frame.
 * \param x the x coordinate to put the frame's pivot at.
 * \param y the y coordinate to put the frame's pivot at.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety This function should only be called on the main thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_RenderSprite
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_RenderFrame(SDL_Renderer *renderer, const SPRAT_Sheet *sheet, int frame, float x, float y);

/**
 * Render a sprite: its frame with the pivot at the sprite's point, scaled,
 * mirrored and rotated around the pivot.
 *
 * Renders as SPRAT_RenderFrame(), placed as SPRAT_Sprite describes.
 *
 * \param renderer the renderer to draw with.
 * \param sprite the sprite.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety This function should only be called on the main thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_RenderFrame
 * \sa SPRAT_GetSpritePoint
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_RenderSprite(SDL_Renderer *renderer, const SPRAT_Sprite *sprite);

/**
 * Render a frame stretched to fill a rectangle, using its nine-slice insets
 * for the 9-grid algorithm.
 *
 * Works as SDL_RenderTexture9Grid(), with the frame's slice as the corner
 * sizes, but also for trimmed frames and frames packed turned. `dstrect`
 * covers the whole original image, trimmed margins included, and the pivot
 * plays no part. Corners shrink proportionally when `dstrect` is smaller than
 * them. A frame without a slice is simply stretched.
 *
 * \param renderer the renderer to draw with.
 * \param sheet the sheet.
 * \param frame the index of the frame.
 * \param scale the scale used to transform the corners of the frame into the
 *              corners of `dstrect`, or 0.0f for an unscaled copy.
 * \param dstrect the rectangle to fill.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety This function should only be called on the main thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_RenderFrame9Grid(SDL_Renderer *renderer, const SPRAT_Sheet *sheet, int frame, float scale, const SDL_FRect *dstrect);

/**
 * Get an animation of a sheet by name.
 *
 * \param sheet the sheet.
 * \param name the animation's name.
 * \returns the animation, or NULL when the sheet has none with that name;
 *          call SDL_GetError() for more information. The animation is valid
 *          until SPRAT_DestroySheet().
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_InitAnimationPlayer
 */
extern SDL_DECLSPEC const SPRAT_Animation * SDLCALL SPRAT_GetAnimationFromName(const SPRAT_Sheet *sheet, const char *name);

/**
 * Set up a player for an animation: playing, looping, at speed 1.0f, from its
 * first frame.
 *
 * \param player the player to set up.
 * \param animation the animation to play, or NULL to play nothing.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_UpdateAnimation
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_InitAnimationPlayer(SPRAT_AnimationPlayer *player, const SPRAT_Animation *animation);

/**
 * Advance a playing animation by a number of seconds, scaled by its speed.
 *
 * Time only moves while you update it, so pausing the game, slow motion or a
 * fixed time step apply to animations too. A looping animation's time wraps
 * around. A non-looping one stops on its last frame, or on its first when
 * playing in reverse. A step that is not finite is ignored.
 *
 * \param player the player.
 * \param seconds the time elapsed, e.g. a difference of SDL_GetTicksNS() / 1e9.
 *
 * \threadsafety It is safe to call this function from any thread, with a
 *               player no other thread uses.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_UpdateAnimation(SPRAT_AnimationPlayer *player, float seconds);

/**
 * Play an animation from its start, or from its end when its speed is
 * negative.
 *
 * \param player the player.
 *
 * \threadsafety It is safe to call this function from any thread, with a
 *               player no other thread uses.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_StopAnimation
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_PlayAnimation(SPRAT_AnimationPlayer *player);

/**
 * Stop an animation and rewind it to where SPRAT_PlayAnimation() starts.
 *
 * \param player the player.
 *
 * \threadsafety It is safe to call this function from any thread, with a
 *               player no other thread uses.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_PlayAnimation
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_StopAnimation(SPRAT_AnimationPlayer *player);

/**
 * Pause an animation, keeping its time.
 *
 * \param player the player.
 *
 * \threadsafety It is safe to call this function from any thread, with a
 *               player no other thread uses.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_ResumeAnimation
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_PauseAnimation(SPRAT_AnimationPlayer *player);

/**
 * Resume a paused animation.
 *
 * \param player the player.
 *
 * \threadsafety It is safe to call this function from any thread, with a
 *               player no other thread uses.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_PauseAnimation
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_ResumeAnimation(SPRAT_AnimationPlayer *player);

/**
 * Query whether a non-looping animation reached its end in the direction it
 * plays.
 *
 * \param player the player.
 * \returns true when the animation is finished; looping animations never
 *          finish.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_IsAnimationFinished(const SPRAT_AnimationPlayer *player);

/**
 * Get the frame an animation shows now.
 *
 * Frame k of the animation covers the times [k / fps, (k + 1) / fps), or
 * (k / fps, (k + 1) / fps] in reverse, so playing from either end shows the
 * frame at that end first.
 *
 * \param player the player.
 * \returns an index into the sheet's frames, or -1 when there is none.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 */
extern SDL_DECLSPEC int SDLCALL SPRAT_GetAnimationFrame(const SPRAT_AnimationPlayer *player);

/**
 * Render the frame an animation shows now, with the animation's flip.
 *
 * To scale, rotate or mirror it, or to place its markers, use a SPRAT_Sprite:
 * set its frame to SPRAT_GetAnimationFrame() and combine your flip with the
 * animation's, `(SDL_FlipMode)(player->animation->flip ^ flip)`, then render
 * it with SPRAT_RenderSprite().
 *
 * \param renderer the renderer to draw with.
 * \param sheet the sheet the animation belongs to.
 * \param player the player.
 * \param x the x coordinate to put the frame's pivot at.
 * \param y the y coordinate to put the frame's pivot at.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information. An animation without frames renders nothing and
 *          succeeds.
 *
 * \threadsafety This function should only be called on the main thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_RenderAnimation(SDL_Renderer *renderer, const SPRAT_Sheet *sheet, const SPRAT_AnimationPlayer *player, float x, float y);

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif /* SDL_SPRAT_H_ */
