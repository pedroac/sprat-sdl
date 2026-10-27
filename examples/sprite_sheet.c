/*
  sprat-sdl example: animated sprites, markers, collisions and nine-slices

  Controls:
      LEFT/RIGHT  Run, and pick up the coins you run into
      SPACE       Swing the sword, which picks up the coins it cuts
      P           Pause and resume the spins
      D           Show pivots, hitboxes and the blade

  Resources: resources/sheet.sprat and resources/atlas.png, packed by sprat-cli
  (see resources/make_resources.sh). Most frames are stored rotated in the atlas.

  This example code is placed in the public domain.
*/

#define SDL_MAIN_USE_CALLBACKS 1    /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_sprat/SDL_sprat.h>

#define SCREEN_WIDTH    800
#define SCREEN_HEIGHT   450
#define HERO_SCALE      3.0f
#define HERO_SPEED      240.0f
#define SWORD_SCALE     2.0f
#define SWING_TIME      0.3f        /* seconds a sword swing lasts */
#define NUM_SPINS       3
#define NUM_COINS       4
#define COIN_SCALE      2.0f
#define COIN_RESPAWN    3.0f        /* seconds a picked coin stays away */

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SPRAT_Sheet *sheet = NULL;

/* Look names up once, then render by index, and play animations with a player each */
static SPRAT_AnimationPlayer run, run_left;
static SPRAT_AnimationPlayer spins[NUM_SPINS];
static const float spin_speeds[NUM_SPINS] = { 1.0f, 0.5f, -1.0f };   /* normal, half speed, reversed */
static SPRAT_AnimationPlayer coin_spin;
static SPRAT_AnimationPlayer coin_pingpong;
static int panel;

/* Sprites are where frames are: rendered there, and their markers placed there */
static SPRAT_Sprite hero, sword;

/* The hero keeps a sprite and a player together, switching between its idle and run animations */
static const SPRAT_Animation *hero_idle, *hero_run;
static SPRAT_AnimationPlayer hero_animation;
static bool facing_left = false;
static SPRAT_Sprite coins[NUM_COINS];

/* Coins along the hero's path, at the height of the hero's chest and of the sword's swing */
static const float coin_x[NUM_COINS] = { 110.0f, 260.0f, 540.0f, 690.0f };
static const float coin_y = 290.0f;
static float coin_away[NUM_COINS];      /* seconds until a picked coin comes back */
static int coins_picked = 0;

static const float ground_y = 380.0f;
static float swing_time = 0.0f;
static bool debug = false;
static Uint64 last_ticks = 0;

/* Render the outline of a sprite's marker, which turns, mirrors and stretches with the sprite */
static void RenderMarker(const SPRAT_Sprite *sprite, const SPRAT_Marker *marker)
{
    SDL_FPoint outline[257];    /* enough for any circle, and polygons of up to 256 vertices */
    int count = SPRAT_GetMarkerOutline(sprite, marker, outline, SDL_arraysize(outline));

    if (count > 0) {
        SDL_SetRenderDrawColor(renderer, 230, 41, 55, 255);
        SDL_RenderLines(renderer, outline, SDL_min(count, (int)SDL_arraysize(outline)));
    }
}

static void RenderDot(float x, float y, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_FRect dot = { x - 3.0f, y - 3.0f, 6.0f, 6.0f };

    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderFillRect(renderer, &dot);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    char *path = NULL;
    int i;

    (void)appstate;
    (void)argc;
    (void)argv;

    SDL_SetAppMetadata("sprat-sdl sprite sheet", "0.1.0", "com.github.pedroac.sprat-sdl.sprite-sheet");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("sprat-sdl - sprite sheet", SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(renderer, 1);

    /* Pixel art: sharp pixels, also when rotated or stretched */
    SDL_SetDefaultTextureScaleMode(renderer, SDL_SCALEMODE_PIXELART);

    /* resources/ is next to the executable */
    SDL_asprintf(&path, "%sresources/sheet.sprat", SDL_GetBasePath());
    sheet = SPRAT_LoadSheet(renderer, path);
    SDL_free(path);
    if (!sheet) {
        SDL_Log("Couldn't load the sprite sheet: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SPRAT_InitAnimationPlayer(&run, SPRAT_GetAnimationFromName(sheet, "run"));
    SPRAT_InitAnimationPlayer(&run_left, SPRAT_GetAnimationFromName(sheet, "run-left"));
    for (i = 0; i < NUM_SPINS; i++) {
        SPRAT_InitAnimationPlayer(&spins[i], SPRAT_GetAnimationFromName(sheet, "spin"));
        spins[i].speed = spin_speeds[i];
    }
    SPRAT_InitAnimationPlayer(&coin_spin, SPRAT_GetAnimationFromName(sheet, "spin"));
    SPRAT_InitAnimationPlayer(&coin_pingpong, SPRAT_GetAnimationFromName(sheet, "spin-pingpong"));
    panel = SPRAT_GetFrameFromName(sheet, "panel");

    hero_idle = SPRAT_GetAnimationFromName(sheet, "idle");
    hero_run = run.animation;
    SPRAT_InitAnimationPlayer(&hero_animation, hero_idle);

    hero = (SPRAT_Sprite){ sheet, -1, SCREEN_WIDTH / 2.0f, ground_y, HERO_SCALE, HERO_SCALE, 0.0, SDL_FLIP_NONE };
    sword = (SPRAT_Sprite){ sheet, SPRAT_GetFrameFromName(sheet, "sword"), 0.0f, 0.0f, SWORD_SCALE, SWORD_SCALE, 0.0, SDL_FLIP_NONE };
    for (i = 0; i < NUM_COINS; i++) {
        coins[i] = (SPRAT_Sprite){ sheet, -1, coin_x[i], coin_y, COIN_SCALE, COIN_SCALE, 0.0, SDL_FLIP_NONE };
    }

    last_ticks = SDL_GetTicksNS();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    (void)appstate;

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        switch (event->key.key) {
        case SDLK_ESCAPE:
            return SDL_APP_SUCCESS;
        case SDLK_SPACE:
            swing_time = SWING_TIME;
            break;
        case SDLK_D:
            debug = !debug;
            break;
        case SDLK_P: {
            int i;
            for (i = 0; i < NUM_SPINS; i++) {
                if (spins[i].playing) {
                    SPRAT_PauseAnimation(&spins[i]);
                } else {
                    SPRAT_ResumeAnimation(&spins[i]);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    const Uint64 now = SDL_GetTicksNS();
    const float dt = (float)(now - last_ticks) / (float)SDL_NS_PER_SECOND;
    const float t = (float)now / (float)SDL_NS_PER_SECOND;
    const SPRAT_Marker *hand, *hero_hitbox, *blade, *coin_hitbox;
    SPRAT_Sprite tilted;
    SDL_FPoint grip;
    SDL_FRect panel_rect;
    char text[32];
    bool running = false;
    int coin_frame, i;

    (void)appstate;
    last_ticks = now;

    /* Update */
    if (keys[SDL_SCANCODE_RIGHT]) {
        hero.x += HERO_SPEED * dt;
        facing_left = false;
        running = true;
    }
    if (keys[SDL_SCANCODE_LEFT]) {
        hero.x -= HERO_SPEED * dt;
        facing_left = true;
        running = true;
    }
    hero.x = SDL_clamp(hero.x, 0.0f, (float)SCREEN_WIDTH);

    SPRAT_UpdateAnimation(&run, dt);
    SPRAT_UpdateAnimation(&run_left, dt);
    for (i = 0; i < NUM_SPINS; i++) {
        SPRAT_UpdateAnimation(&spins[i], dt);
    }
    SPRAT_UpdateAnimation(&coin_spin, dt);
    SPRAT_UpdateAnimation(&coin_pingpong, dt);

    if (swing_time > 0.0f) {
        swing_time -= dt;
    }

    /* Setting the animation every update restarts it only when it changes. The player sets the
       hero's frame and flip; facing left mirrors with the scale, which it leaves alone */
    SPRAT_SetAnimation(&hero_animation, running ? hero_run : hero_idle);
    SPRAT_UpdateSpriteAnimation(&hero, &hero_animation, dt);
    hero.scale_x = facing_left ? -HERO_SCALE : HERO_SCALE;
    coin_frame = SPRAT_GetAnimationFrame(&coin_spin);

    /* The sword is held by the hero's "hand" marker, mirrored with the hero */
    hand = SPRAT_GetSpriteMarker(&hero, "hand");
    if (!hand || !SPRAT_GetMarkerCenter(&hero, hand, &grip)) {
        grip.x = hero.x;
        grip.y = hero.y;
    }
    sword.x = grip.x;
    sword.y = grip.y;
    sword.angle = 60.0f - ((swing_time > 0.0f) ? 150.0f * (1.0f - swing_time / SWING_TIME) : 0.0f);
    if (facing_left) {
        sword.angle = -sword.angle;
    }

    /* A coin is picked when the hero's hitbox overlaps the coin's, or the sword's blade does
       while it swings */
    hero_hitbox = SPRAT_GetSpriteMarker(&hero, "hitbox");
    blade = SPRAT_GetSpriteMarker(&sword, "blade");
    coin_hitbox = SPRAT_GetFrameMarker(sheet, coin_frame, "hitbox");   /* the coins spin together */
    for (i = 0; i < NUM_COINS; i++) {
        SPRAT_SetSpriteAnimationFrame(&coins[i], &coin_spin);
        if (coin_away[i] > 0.0f) {
            coin_away[i] -= dt;
            continue;
        }
        if (SPRAT_MarkersOverlap(&hero, hero_hitbox, &coins[i], coin_hitbox) ||
            (swing_time > 0.0f && SPRAT_MarkersOverlap(&sword, blade, &coins[i], coin_hitbox))) {
            coin_away[i] = COIN_RESPAWN;
            coins_picked++;
        }
    }

    /* The panel breathes, to show its corners stay sharp */
    panel_rect.x = 20.0f;
    panel_rect.y = 20.0f;
    panel_rect.w = 300.0f + 60.0f * SDL_sinf(t * 1.5f);
    panel_rect.h = 110.0f + 20.0f * SDL_sinf(t * 2.0f);

    /* Render */
    SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
    SDL_RenderClear(renderer);

    SPRAT_RenderFrame9Grid(renderer, sheet, panel, 2.0f, &panel_rect);
    SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
    SDL_RenderDebugText(renderer, panel_rect.x + 24.0f, panel_rect.y + 28.0f, "SPRAT_RenderFrame9Grid()");
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderDebugText(renderer, panel_rect.x + 24.0f, panel_rect.y + 56.0f, "LEFT/RIGHT run, SPACE swing");
    SDL_RenderDebugText(renderer, panel_rect.x + 24.0f, panel_rect.y + 70.0f, "D pivots and hitboxes, P pause");

    /* Animations, with the flip and loop mode the sheet gave each one */
    SDL_SetRenderDrawColor(renderer, 130, 130, 130, 255);
    SDL_RenderDebugText(renderer, 470.0f, 40.0f, "run");
    SDL_RenderDebugText(renderer, 560.0f, 40.0f, "run-left (flip h)");
    SDL_RenderDebugText(renderer, 728.0f, 40.0f, "spin");
    SDL_RenderDebugText(renderer, 380.0f, 40.0f, "pingpong");
    SPRAT_RenderAnimation(renderer, sheet, &coin_pingpong, 410.0f, 90.0f);
    SPRAT_RenderAnimation(renderer, sheet, &run, 490.0f, 150.0f);
    SPRAT_RenderAnimation(renderer, sheet, &run_left, 630.0f, 150.0f);
    for (i = 0; i < NUM_SPINS; i++) {
        char label[16];

        SPRAT_RenderAnimation(renderer, sheet, &spins[i], 745.0f, 90.0f + (float)i * 40.0f);
        SDL_snprintf(label, sizeof(label), "x%g", spin_speeds[i]);
        SDL_SetRenderDrawColor(renderer, 130, 130, 130, 255);
        SDL_RenderDebugText(renderer, 762.0f, 86.0f + (float)i * 40.0f, label);
    }

    /* Every frame, rotated around its pivot */
    tilted = (SPRAT_Sprite){ sheet, 0, 0.0f, 230.0f, 0.75f, 0.75f, 0.0, SDL_FLIP_NONE };
    for (i = 0; i < sheet->num_frames; i++) {
        tilted.frame = i;
        tilted.x = 40.0f + (float)i * 46.0f;
        tilted.angle = 15.0 * SDL_sin((double)t + i);
        SPRAT_RenderSprite(renderer, &tilted);
        if (debug) {
            RenderDot(tilted.x, tilted.y, 230, 41, 55);
        }
    }

    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderLine(renderer, 0.0f, ground_y, (float)SCREEN_WIDTH, ground_y);

    /* Coins still to pick, and the count of those picked */
    for (i = 0; i < NUM_COINS; i++) {
        if (coin_away[i] <= 0.0f) {
            SPRAT_RenderSprite(renderer, &coins[i]);
            if (debug) {
                RenderMarker(&coins[i], coin_hitbox);
            }
        }
    }
    SDL_snprintf(text, sizeof(text), "coins picked: %d", coins_picked);
    SDL_SetRenderDrawColor(renderer, 130, 130, 130, 255);
    SDL_RenderDebugText(renderer, 20.0f, (float)SCREEN_HEIGHT - 30.0f, text);

    /* Controlled hero, with the sword rendered behind when facing left */
    if (facing_left) {
        SPRAT_RenderSprite(renderer, &sword);
    }
    SPRAT_RenderSprite(renderer, &hero);
    if (!facing_left) {
        SPRAT_RenderSprite(renderer, &sword);
    }

    if (debug) {
        RenderMarker(&hero, hero_hitbox);
        RenderMarker(&sword, blade);
        RenderDot(hero.x, hero.y, 230, 41, 55);
        RenderDot(sword.x, sword.y, 0, 121, 241);
    }

    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;

    /* Destroy the sheet before the renderer its textures belong to; SDL destroys the
       window and renderer itself */
    SPRAT_DestroySheet(sheet);
}
