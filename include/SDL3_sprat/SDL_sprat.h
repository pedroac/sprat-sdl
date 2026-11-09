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
 * const SPRAT_Animation *idle = SPRAT_GetAnimationFromName(sheet, "idle");
 * const SPRAT_Animation *run = SPRAT_GetAnimationFromName(sheet, "run");
 * SPRAT_Sprite hero = { sheet, -1, 400.0f, 300.0f, 1.0f, 1.0f, 0.0, SDL_FLIP_NONE };
 * SPRAT_AnimationPlayer player;
 * SPRAT_InitAnimationPlayer(&player, idle);
 * ...
 * SPRAT_SetAnimation(&player, running ? run : idle);     // restarts only on a change
 * SPRAT_UpdateSpriteAnimation(&hero, &player, seconds);   // sets hero's frame and flip
 * const SPRAT_Marker *hitbox = SPRAT_GetSpriteMarker(&hero, "hitbox");
 * SPRAT_RenderSprite(renderer, &hero);
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
#define SDL_SPRAT_MINOR_VERSION   6

/**
 * The current micro (or patchlevel) version of sprat-sdl headers.
 *
 * If this were sprat-sdl version 3.2.1, this value would be 1.
 *
 * \since This macro is available since sprat-sdl 0.1.0.
 */
#define SDL_SPRAT_MICRO_VERSION   1

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
 * \anchor SPRAT_Version
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
 * How an animation plays: sprat-cli's `loop`, `once`, `pingpong` or
 * `once-pingpong`.
 *
 * The values are flags: SPRAT_LOOPMODE_ONCE plays one pass and stops, and
 * SPRAT_LOOPMODE_PINGPONG walks the frames back after the last one, without
 * repeating the frames at either end. For frames 0 1 2:
 *
 * - SPRAT_LOOPMODE_REPEAT plays 0 1 2, 0 1 2, ...
 * - SPRAT_LOOPMODE_ONCE plays 0 1 2, stopping on 2.
 * - SPRAT_LOOPMODE_PINGPONG plays 0 1 2 1, 0 1 2 1, ...
 * - SPRAT_LOOPMODE_ONCE_PINGPONG plays 0 1 2 1 0, stopping on 0.
 *
 * \since This enum is available since sprat-sdl 0.2.0.
 *
 * \sa SPRAT_AnimationPlayer
 */
typedef enum SPRAT_LoopMode
{
    SPRAT_LOOPMODE_REPEAT = 0,          /**< repeat forward */
    SPRAT_LOOPMODE_ONCE = 1,            /**< play one pass and stop on the last frame */
    SPRAT_LOOPMODE_PINGPONG = 2,        /**< play forward then backward, repeating */
    SPRAT_LOOPMODE_ONCE_PINGPONG = (SPRAT_LOOPMODE_ONCE | SPRAT_LOOPMODE_PINGPONG) /**< play forward then backward once, stopping on the first frame */
} SPRAT_LoopMode;

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
 * has `run`'s frames (the same array) and loop mode, and a flip of
 * SDL_FLIP_HORIZONTAL. Sheets written before loop modes (`sprat-raw 1`) load
 * with SPRAT_LOOPMODE_REPEAT.
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
    SPRAT_LoopMode loop_mode;       /**< how the animation plays (since sprat-sdl 0.2.0) */
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
    int num_puppets;                /**< number of puppets (cutout rigs), see SPRAT_CreatePuppet() (since sprat-sdl 0.6.0) */
} SPRAT_Sheet;

/**
 * The playback state of an animation.
 *
 * Keep one per animated thing and update it every frame. A player allocates
 * nothing, so copy it or drop it freely. Its fields are yours to read and
 * set, `time` included.
 *
 * SPRAT_InitAnimationPlayer() copies the animation's loop mode into the
 * player's `loop_mode`; set it to play the animation another way:
 *
 * ```c
 * SPRAT_InitAnimationPlayer(&attack, SPRAT_GetAnimationFromName(sheet, "attack"));
 * attack.loop_mode = SPRAT_LOOPMODE_ONCE;
 * ```
 *
 * `time` covers one pass of the animation: `num_frames / fps` seconds, or
 * with ping-pong and two frames or more, `(2 * num_frames - 2) / fps`
 * (`(2 * num_frames - 1) / fps` played once).
 *
 * \since This struct is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_InitAnimationPlayer
 * \sa SPRAT_UpdateAnimation
 */
typedef struct SPRAT_AnimationPlayer
{
    const SPRAT_Animation *animation; /**< the animation played, NULL for none */
    float time;                     /**< seconds into one pass of the animation, from 0 to its duration */
    float speed;                    /**< playback rate: 1.0f plays at the animation's fps, negative plays in reverse */
    bool playing;                   /**< true when SPRAT_UpdateAnimation() advances the time */
    SPRAT_LoopMode loop_mode;       /**< how the animation plays, the animation's own by default (since sprat-sdl 0.2.0; it replaces `bool loop`) */
} SPRAT_AnimationPlayer;

/**
 * A frame placed on the screen: a sheet's frame, where its pivot is, and how
 * it is scaled, mirrored and rotated around the pivot.
 *
 * A sprite is not an object of the library: it is a plain struct, owned by the
 * program, that SPRAT_RenderSprite() renders and the marker functions place
 * markers with, so what is drawn and what collides always agree. Keep a
 * sprite as part of the program's state, and change its fields directly;
 * SPRAT_UpdateSpriteAnimation() sets its `frame` and `flip` from an animation
 * player each update, so mirror an animated sprite with a negative scale.
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
 * \sa SPRAT_UpdateSpriteAnimation
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
 * A cutout puppet placed on the screen: parts joined by their markers, each
 * showing one of its variants (a sprite or an animation), posed by poses and
 * animated by sequences of them, played and rendered as sprat-gui's Puppet
 * workspace previews it.
 *
 * Create one per puppet on screen with SPRAT_CreatePuppet(). Every update,
 * reset its pose, apply the sequences and poses it plays (later ones win
 * where they set the same part), then render it:
 *
 * ```c
 * SPRAT_UpdatePuppet(kid, seconds);          // the animations its parts show
 * kid->x = x;
 * kid->flip = facing_left ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
 * SPRAT_ResetPuppetPose(kid);
 * SPRAT_ApplyPuppetSequence(kid, walk, walk_time);
 * SPRAT_RenderPuppet(renderer, kid);
 * ```
 *
 * `x`, `y`, `scale`, `angle` and `flip` place the whole puppet around its
 * pivot, as they place a SPRAT_Sprite; set them directly. Its parts' own
 * scales, fades and tints come from its pose.
 *
 * \since This struct is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_CreatePuppet
 * \sa SPRAT_RenderPuppet
 */
typedef struct SPRAT_Puppet
{
    const SPRAT_Sheet *sheet;       /**< the sheet the puppet belongs to */
    const char *name;               /**< the puppet's name, UTF-8 */
    int num_parts;                  /**< number of parts, in render order (the first at the back) */
    float x;                        /**< the x coordinate of the puppet's pivot */
    float y;                        /**< the y coordinate of the puppet's pivot */
    float scale;                    /**< the scale around the pivot, 1.0f by default */
    double angle;                   /**< the clockwise rotation around the pivot, in degrees */
    SDL_FlipMode flip;              /**< the mirroring around the pivot */
    float time;                     /**< seconds its parts' animations have played, see SPRAT_UpdatePuppet() */
    struct SPRAT_PuppetPose *pose;  /**< the pose of each part (private) */
} SPRAT_Puppet;

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
 * \anchor SPRAT_LoadSheet
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
 * \anchor SPRAT_LoadSheet_IO
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
 * \anchor SPRAT_DestroySheet
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
 * \anchor SPRAT_GetFrameFromName
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
 * \anchor SPRAT_GetFrameMarker
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
 * \anchor SPRAT_GetSpriteMarker
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
 * \anchor SPRAT_GetSpritePoint
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
 * \anchor SPRAT_GetMarkerCenter
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
 * \anchor SPRAT_GetMarkerOutline
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
 * \anchor SPRAT_MarkersOverlap
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
 * \anchor SPRAT_RenderFrame
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
 * \anchor SPRAT_RenderSprite
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
 * \anchor SPRAT_RenderFrame9Grid
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
 * \anchor SPRAT_GetAnimationFromName
 */
extern SDL_DECLSPEC const SPRAT_Animation * SDLCALL SPRAT_GetAnimationFromName(const SPRAT_Sheet *sheet, const char *name);

/**
 * Set up a player for an animation: playing from its first frame, at speed
 * 1.0f, with the animation's loop mode.
 *
 * \param player the player to set up.
 * \param animation the animation to play, or NULL to play nothing.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 *
 * \sa SPRAT_SetAnimation
 * \sa SPRAT_UpdateAnimation
 * \anchor SPRAT_InitAnimationPlayer
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_InitAnimationPlayer(SPRAT_AnimationPlayer *player, const SPRAT_Animation *animation);

/**
 * Set the animation a player plays: restart with it, with its loop mode and
 * the player's speed, unless it is the one played.
 *
 * The one played is left as it is, stopped or finished included, so set the
 * animation every update, and it restarts only when it changes:
 *
 * ```c
 * SPRAT_SetAnimation(&player, moving ? walk : idle);
 * SPRAT_UpdateSpriteAnimation(&sprite, &player, seconds);
 * ```
 *
 * An animation is the one played when it is the same pointer, as the sheet's
 * animations are, so any other animation restarts the player, an alias or a
 * copy included. To play the one played another way, set the player's `speed`
 * or `loop_mode`, or the sprite's scale, rather than switching to a copy.
 *
 * \param player the player.
 * \param animation the animation to play, or NULL to play nothing. It must
 *                  stay valid while the player plays it.
 *
 * \threadsafety It is safe to call this function from any thread, with a
 *               player no other thread uses.
 *
 * \since This function is available since sprat-sdl 0.3.0.
 *
 * \sa SPRAT_InitAnimationPlayer
 * \sa SPRAT_PlayAnimation
 * \sa SPRAT_UpdateSpriteAnimation
 * \anchor SPRAT_SetAnimation
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_SetAnimation(SPRAT_AnimationPlayer *player, const SPRAT_Animation *animation);

/**
 * Advance a playing animation by a number of seconds, scaled by its speed.
 *
 * Time only moves while you update it, so pausing the game, slow motion or a
 * fixed time step apply to animations too. A repeating animation's time wraps
 * around. One with SPRAT_LOOPMODE_ONCE stops at the end of its pass, or at its
 * start when playing in reverse. A step that is not finite is ignored.
 *
 * \param player the player.
 * \param seconds the time elapsed, e.g. a difference of SDL_GetTicksNS() / 1e9.
 *
 * \threadsafety It is safe to call this function from any thread, with a
 *               player no other thread uses.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 * \anchor SPRAT_UpdateAnimation
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
 * \anchor SPRAT_PlayAnimation
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
 * \anchor SPRAT_StopAnimation
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
 * \anchor SPRAT_PauseAnimation
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
 * \anchor SPRAT_ResumeAnimation
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_ResumeAnimation(SPRAT_AnimationPlayer *player);

/**
 * Query whether an animation played with SPRAT_LOOPMODE_ONCE reached the end
 * of its pass in the direction it plays.
 *
 * \param player the player.
 * \returns true when the animation is finished; repeating animations never
 *          finish.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 * \anchor SPRAT_IsAnimationFinished
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_IsAnimationFinished(const SPRAT_AnimationPlayer *player);

/**
 * Get the frame an animation shows now.
 *
 * Step k of a pass covers the times [k / fps, (k + 1) / fps), or
 * (k / fps, (k + 1) / fps] in reverse, so playing from either end shows the
 * frame at that end first. With ping-pong, the steps past the last frame walk
 * the frames back.
 *
 * \param player the player.
 * \returns an index into the sheet's frames, or -1 when there is none.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.1.0.
 * \anchor SPRAT_GetAnimationFrame
 */
extern SDL_DECLSPEC int SDLCALL SPRAT_GetAnimationFrame(const SPRAT_AnimationPlayer *player);

/**
 * Set a sprite's frame to the one an animation shows now, and its flip to the
 * animation's.
 *
 * The sprite's flip is replaced, not combined: mirror an animated sprite with
 * a negative scale instead. The rest of the sprite is left as it is. Use it
 * to set several sprites from one player; SPRAT_UpdateSpriteAnimation() also
 * advances the player.
 *
 * \param sprite the sprite to set.
 * \param player the player. With no animation, or one without frames, the
 *               sprite's frame is set to -1, which renders nothing and fails.
 *
 * \threadsafety It is safe to call this function from any thread, with a
 *               sprite no other thread uses.
 *
 * \since This function is available since sprat-sdl 0.3.0.
 *
 * \sa SPRAT_UpdateSpriteAnimation
 * \sa SPRAT_GetAnimationFrame
 * \anchor SPRAT_SetSpriteAnimationFrame
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_SetSpriteAnimationFrame(SPRAT_Sprite *sprite, const SPRAT_AnimationPlayer *player);

/**
 * Advance an animation by a number of seconds, then set a sprite's frame and
 * flip from it.
 *
 * Works as SPRAT_UpdateAnimation() followed by
 * SPRAT_SetSpriteAnimationFrame(). After SPRAT_StopAnimation() or
 * SPRAT_PlayAnimation(), 0 seconds only sets the sprite.
 *
 * \param sprite the sprite to set, or NULL to only advance the player.
 * \param player the player.
 * \param seconds the time elapsed, e.g. a difference of SDL_GetTicksNS() / 1e9.
 *
 * \threadsafety It is safe to call this function from any thread, with a
 *               sprite and a player no other thread uses.
 *
 * \since This function is available since sprat-sdl 0.3.0.
 *
 * \sa SPRAT_SetAnimation
 * \sa SPRAT_SetSpriteAnimationFrame
 * \sa SPRAT_UpdateAnimation
 * \anchor SPRAT_UpdateSpriteAnimation
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_UpdateSpriteAnimation(SPRAT_Sprite *sprite, SPRAT_AnimationPlayer *player, float seconds);

/**
 * Render the frame an animation shows now, with the animation's flip.
 *
 * To scale, rotate or mirror it, use SPRAT_RenderAnimationRotated(). To place
 * its markers too, keep a SPRAT_Sprite in step with the player with
 * SPRAT_UpdateSpriteAnimation(), and render it with SPRAT_RenderSprite().
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
 *
 * \sa SPRAT_RenderAnimationRotated
 * \anchor SPRAT_RenderAnimation
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_RenderAnimation(SDL_Renderer *renderer, const SPRAT_Sheet *sheet, const SPRAT_AnimationPlayer *player, float x, float y);

/**
 * Render the frame an animation shows now, scaled, mirrored and rotated
 * around its pivot.
 *
 * Renders as SPRAT_RenderSprite() a sprite of the frame placed with these
 * values, its flip combined with the animation's: flipping an animation
 * already flipped on an axis renders it unflipped on that axis.
 *
 * \param renderer the renderer to draw with.
 * \param sheet the sheet the animation belongs to.
 * \param player the player.
 * \param x the x coordinate to put the frame's pivot at.
 * \param y the y coordinate to put the frame's pivot at.
 * \param scale_x the horizontal scale, negative to mirror.
 * \param scale_y the vertical scale, negative to mirror.
 * \param angle the clockwise rotation around the pivot, in degrees.
 * \param flip the mirroring around the pivot, combined with the animation's.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information. An animation without frames renders nothing and
 *          succeeds.
 *
 * \threadsafety This function should only be called on the main thread.
 *
 * \since This function is available since sprat-sdl 0.3.0.
 *
 * \sa SPRAT_RenderAnimation
 * \sa SPRAT_RenderSprite
 * \anchor SPRAT_RenderAnimationRotated
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_RenderAnimationRotated(SDL_Renderer *renderer, const SPRAT_Sheet *sheet, const SPRAT_AnimationPlayer *player,
                                                              float x, float y, float scale_x, float scale_y, double angle, SDL_FlipMode flip);

/**
 * Create a puppet of a sheet, in its rest pose, with its pivot at (0, 0).
 *
 * In the rest pose every part shows its first variant, unturned, and
 * positioned parts are where the rig puts them.
 *
 * \param sheet the sheet.
 * \param name the puppet's name.
 * \returns a new puppet, or NULL on failure; call SDL_GetError() for more
 *          information. Destroy it with SPRAT_DestroyPuppet() before the
 *          sheet.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_DestroyPuppet
 * \anchor SPRAT_CreatePuppet
 */
extern SDL_DECLSPEC SPRAT_Puppet * SDLCALL SPRAT_CreatePuppet(const SPRAT_Sheet *sheet, const char *name);

/**
 * Destroy a puppet.
 *
 * \param puppet the puppet, or NULL.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_CreatePuppet
 * \anchor SPRAT_DestroyPuppet
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_DestroyPuppet(SPRAT_Puppet *puppet);

/**
 * Get a part of a puppet by name.
 *
 * \param puppet the puppet.
 * \param name the part's name.
 * \returns the index of the part, or -1 when the puppet has none with that
 *          name; call SDL_GetError() for more information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_GetPuppetPartSprite
 * \anchor SPRAT_GetPuppetPartFromName
 */
extern SDL_DECLSPEC int SDLCALL SPRAT_GetPuppetPartFromName(const SPRAT_Puppet *puppet, const char *name);

/**
 * Get a pose of a puppet by name.
 *
 * \param puppet the puppet.
 * \param name the pose's name.
 * \returns the index of the pose, or -1 when the puppet has none with that
 *          name; call SDL_GetError() for more information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_ApplyPuppetPose
 * \anchor SPRAT_GetPuppetPoseFromName
 */
extern SDL_DECLSPEC int SDLCALL SPRAT_GetPuppetPoseFromName(const SPRAT_Puppet *puppet, const char *name);

/**
 * Get a sequence of a puppet by name.
 *
 * \param puppet the puppet.
 * \param name the sequence's name.
 * \returns the index of the sequence, or -1 when the puppet has none with
 *          that name; call SDL_GetError() for more information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_ApplyPuppetSequence
 * \anchor SPRAT_GetPuppetSequenceFromName
 */
extern SDL_DECLSPEC int SDLCALL SPRAT_GetPuppetSequenceFromName(const SPRAT_Puppet *puppet, const char *name);

/**
 * Reset a puppet to its rest pose.
 *
 * \param puppet the puppet.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety Don't use the same puppet from several threads at once.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_ApplyPuppetPose
 * \sa SPRAT_ApplyPuppetSequence
 * \anchor SPRAT_ResetPuppetPose
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_ResetPuppetPose(SPRAT_Puppet *puppet);

/**
 * Apply a pose to a puppet: set what the pose sets, over its current pose.
 *
 * \param puppet the puppet.
 * \param pose the index of the pose.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety Don't use the same puppet from several threads at once.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_GetPuppetPoseFromName
 * \anchor SPRAT_ApplyPuppetPose
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_ApplyPuppetPose(SPRAT_Puppet *puppet, int pose);

/**
 * Apply a sequence to a puppet as it is some seconds after it started, over
 * the pose it started from.
 *
 * Reset the pose and apply sequences every update, so each starts from the
 * same pose. A sequence plays whole frames at its fps: frame `k` of a step's
 * `in` moves the values its pose sets `(k + 1) / in` of the way (parts that
 * ease by step wait for the last `in` frame), variants switch when the pose
 * arrives, then the step holds. Ping-pong plays the steps back again, a loop
 * starts each cycle where the last ended, and once holds its last frame.
 *
 * \param puppet the puppet.
 * \param sequence the index of the sequence.
 * \param seconds the seconds since the sequence started.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety Don't use the same puppet from several threads at once.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_GetPuppetSequenceFromName
 * \sa SPRAT_IsPuppetSequenceFinished
 * \anchor SPRAT_ApplyPuppetSequence
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_ApplyPuppetSequence(SPRAT_Puppet *puppet, int sequence, float seconds);

/**
 * Get the seconds one cycle of a puppet's sequence takes.
 *
 * \param puppet the puppet.
 * \param sequence the index of the sequence.
 * \returns the duration of one cycle (for ping-pong, the way back too), or a
 *          negative value on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 * \anchor SPRAT_GetPuppetSequenceDuration
 */
extern SDL_DECLSPEC float SDLCALL SPRAT_GetPuppetSequenceDuration(const SPRAT_Puppet *puppet, int sequence);

/**
 * Check if a puppet's sequence that plays once has played its whole cycle.
 *
 * \param puppet the puppet.
 * \param sequence the index of the sequence.
 * \param seconds the seconds since the sequence started.
 * \returns true when it has; looping sequences never finish.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 * \anchor SPRAT_IsPuppetSequenceFinished
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_IsPuppetSequenceFinished(const SPRAT_Puppet *puppet, int sequence, float seconds);

/**
 * Advance the animations a puppet's parts show.
 *
 * Each animation plays from the puppet's `time`, at its own fps and loop
 * mode.
 *
 * \param puppet the puppet.
 * \param seconds the seconds to advance by.
 *
 * \threadsafety Don't use the same puppet from several threads at once.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 * \anchor SPRAT_UpdatePuppet
 */
extern SDL_DECLSPEC void SDLCALL SPRAT_UpdatePuppet(SPRAT_Puppet *puppet, float seconds);

/**
 * Get a part of a puppet as the sprite it is rendered as.
 *
 * The sprite's markers and collisions work as any sprite's. A part isn't
 * rendered when it shows nothing, hangs by a marker its frame lacks (a missing
 * "pivot" marker is the frame's pivot), or hangs from a part that isn't
 * rendered; its sprite's frame is then -1. Every part is placed, since a part
 * follows its parents: to render them all, use SPRAT_RenderPuppet().
 *
 * \param puppet the puppet.
 * \param part the index of the part.
 * \param sprite filled in with the part's sprite.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety Don't use the same puppet from several threads at once.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_GetPuppetPartColor
 * \anchor SPRAT_GetPuppetPartSprite
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_GetPuppetPartSprite(SPRAT_Puppet *puppet, int part, SPRAT_Sprite *sprite);

/**
 * Get the color a part of a puppet is rendered with: its tint and opacity.
 *
 * SPRAT_RenderPuppet() sets it as the texture's color and alpha modulation.
 *
 * \param puppet the puppet.
 * \param part the index of the part.
 * \param color filled in with the part's color, each channel 0.0f to 1.0f.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety Don't use the same puppet from several threads at once.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 * \anchor SPRAT_GetPuppetPartColor
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_GetPuppetPartColor(const SPRAT_Puppet *puppet, int part, SDL_FColor *color);

/**
 * Render a puppet's parts in order, each tinted and faded by its pose.
 *
 * Each part's color is combined with its atlas texture's own color and alpha
 * modulation, which are restored afterwards.
 *
 * \param renderer the renderer to draw with.
 * \param puppet the puppet.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety This function should only be called on the main thread.
 *
 * \since This function is available since sprat-sdl 0.6.0.
 *
 * \sa SPRAT_GetPuppetPartSprite
 * \anchor SPRAT_RenderPuppet
 */
extern SDL_DECLSPEC bool SDLCALL SPRAT_RenderPuppet(SDL_Renderer *renderer, SPRAT_Puppet *puppet);

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif /* SDL_SPRAT_H_ */
