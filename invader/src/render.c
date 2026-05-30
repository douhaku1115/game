#include "game.h"
#include <SDL.h>
#include <string.h>
#include <stdio.h>

/* ── Sprite data (8×8 pixels, 2 animation frames) ────────────────────── */

/* Type 0 – octopus (top rows, 30 pt) */
static const uint8_t SPR_A[2][8] = {
    { 0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x5A, 0xA5 },
    { 0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0xA5, 0x5A },
};
/* Type 1 – crab (mid rows, 20 pt) */
static const uint8_t SPR_B[2][8] = {
    { 0x42, 0xE7, 0xFF, 0xDB, 0xFF, 0x5A, 0x24, 0x81 },
    { 0xC3, 0xE7, 0xFF, 0xDB, 0xFF, 0x5A, 0x42, 0x81 },
};
/* Type 2 – squid (bottom rows, 10 pt) */
static const uint8_t SPR_C[2][8] = {
    { 0x3C, 0x7E, 0xFF, 0xDB, 0xFF, 0xFF, 0x66, 0x99 },
    { 0x3C, 0x7E, 0xFF, 0xDB, 0xFF, 0xFF, 0x99, 0x66 },
};
/* UFO */
static const uint8_t SPR_UFO[6] = {
    0x18, 0x7E, 0xFF, 0xDB, 0xFF, 0x66
};
/* Player ship */
static const uint8_t SPR_SHIP[8] = {
    0x18, 0x18, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* 5×7 pixel font for digits 0-9 and letters A/E/G/H/I/L/M/N/O/P/R/S/T/U/V/W/X/Y */
static const uint8_t FONT5[128][7] = {
    ['0'] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },
    ['1'] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    ['2'] = { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F },
    ['3'] = { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E },
    ['4'] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
    ['5'] = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    ['6'] = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },
    ['7'] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    ['8'] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
    ['9'] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
    ['A'] = { 0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11 },
    ['C'] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },
    ['E'] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
    ['G'] = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E },
    ['H'] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    ['I'] = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
    ['L'] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
    ['M'] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 },
    ['N'] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
    ['O'] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    ['P'] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
    ['R'] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
    ['S'] = { 0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E },
    ['T'] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    ['U'] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    ['V'] = { 0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04 },
    ['W'] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },
    ['X'] = { 0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11 },
    ['Y'] = { 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04 },
    ['-'] = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 },
    [' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

/* ── low-level helpers ─────────────────────────────────────────────────── */

static void draw_sprite8(SDL_Renderer *r,
                          const uint8_t *rows, int nrows,
                          int x, int y, int scale,
                          Uint8 R, Uint8 G, Uint8 B)
{
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    for (int row = 0; row < nrows; row++) {
        for (int bit = 7; bit >= 0; bit--) {
            if (rows[row] & (1 << bit)) {
                SDL_Rect rc = {
                    x + (7 - bit) * scale,
                    y + row * scale,
                    scale, scale
                };
                SDL_RenderFillRect(r, &rc);
            }
        }
    }
}

static void draw_char(SDL_Renderer *r, char c, int x, int y, int scale,
                       Uint8 R, Uint8 G, Uint8 B)
{
    if ((unsigned char)c >= 128) return;
    draw_sprite8(r, FONT5[(unsigned char)c], 7, x, y, scale, R, G, B);
}

static void draw_string(SDL_Renderer *r, const char *s, int x, int y,
                         int scale, Uint8 R, Uint8 G, Uint8 B)
{
    for (; *s; s++, x += (5 + 1) * scale)
        draw_char(r, *s, x, y, scale, R, G, B);
}

static void draw_number(SDL_Renderer *r, int n, int x, int y, int digits,
                         int scale, Uint8 R, Uint8 G, Uint8 B)
{
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%0*d", digits, n);
    draw_string(r, buf, x, y, scale, R, G, B);
    (void)len;
}

/* ── entity renderers ──────────────────────────────────────────────────── */

static void render_invaders(SDL_Renderer *r, const Game *g)
{
    static const Uint8 COL[3][3] = {
        { 80, 255, 80  },   /* type 0 green */
        { 80, 220, 255 },   /* type 1 cyan  */
        { 255,255, 255 },   /* type 2 white */
    };
    for (int row = 0; row < INV_ROWS; row++) {
        for (int col = 0; col < INV_COLS; col++) {
            const Invader *iv = &g->inv[row][col];
            if (!iv->alive) continue;
            int x = (int)(iv->x + g->inv_ox);
            int y = (int)(iv->y + g->inv_oy);
            const uint8_t *spr = (iv->type == 0) ? SPR_A[iv->frame] :
                                  (iv->type == 1) ? SPR_B[iv->frame] :
                                                    SPR_C[iv->frame];
            draw_sprite8(r, spr, 8, x, y, 3,
                         COL[iv->type][0], COL[iv->type][1], COL[iv->type][2]);
        }
    }
}

static void render_player(SDL_Renderer *r, const Game *g)
{
    if (!g->player_alive) return;
    if (g->state == STATE_DYING && g->flash) return;
    int x = (int)(g->player_x - PLAYER_W / 2.0f);
    int y = PLAYER_Y;
    /* draw from bottom-up ship pattern (8 wide × 8 tall, scaled ×4 width 32 h 32 → trim) */
    draw_sprite8(r, SPR_SHIP, 8, x, y, 4, 100, 255, 100);
}

static void render_ufo(SDL_Renderer *r, const Game *g)
{
    if (!g->ufo.active) return;
    /* flash red */
    Uint32 t = SDL_GetTicks();
    Uint8 red = ((t / 100) & 1) ? 255 : 180;
    /* UFO is 6 rows of 8 bits, scale 6 → 48×36px */
    draw_sprite8(r, SPR_UFO, 6, (int)g->ufo.x, (int)g->ufo.y, 6, red, 40, 40);
}

static void render_bullets(SDL_Renderer *r, const Game *g)
{
    /* player bullet – bright yellow */
    if (g->pbullet.active) {
        SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
        SDL_Rect rc = { (int)g->pbullet.x, (int)g->pbullet.y, BULLET_W, PBULLET_H };
        SDL_RenderFillRect(r, &rc);
    }
    /* invader bullets – orange */
    SDL_SetRenderDrawColor(r, 255, 140, 0, 255);
    for (int i = 0; i < MAX_INV_BULLETS; i++) {
        if (!g->ibullets[i].active) continue;
        SDL_Rect rc = { (int)g->ibullets[i].x, (int)g->ibullets[i].y, BULLET_W, IBULLET_H };
        SDL_RenderFillRect(r, &rc);
    }
}

static void render_shields(SDL_Renderer *r, const Game *g)
{
    int sx0[SHIELD_COUNT] = { 80, 240, 400, 560 };
    int sy0 = WIN_H - 160;
    int pw  = 4;
    SDL_SetRenderDrawColor(r, 80, 255, 80, 255);
    for (int s = 0; s < SHIELD_COUNT; s++) {
        for (int row = 0; row < SHIELD_ROWS; row++) {
            if (!g->shields[s][row]) continue;
            for (int col = 0; col < SHIELD_COLS; col++) {
                if (g->shields[s][row] & (1u << col)) {
                    SDL_Rect rc = {
                        sx0[s] + col * pw,
                        sy0    + row * pw,
                        pw, pw
                    };
                    SDL_RenderFillRect(r, &rc);
                }
            }
        }
    }
}

static void render_hud(SDL_Renderer *r, const Game *g)
{
    /* top line */
    draw_string(r, "SCORE", 10, 8, 2, 255, 255, 255);
    draw_number(r, g->score, 10, 24, 5, 2, 255, 255, 0);

    draw_string(r, "HI-SCORE", WIN_W/2 - 56, 8, 2, 255, 255, 255);
    draw_number(r, g->hi_score, WIN_W/2 - 30, 24, 5, 2, 255, 255, 0);

    /* stage indicator */
    {
        char sbuf[16];
        snprintf(sbuf, sizeof(sbuf), "STAGE %d", g->stage);
        draw_string(r, sbuf, WIN_W/2 - 56, 8, 2, 255, 200, 0);
    }

    draw_string(r, "LIVES", WIN_W - 120, 8, 2, 255, 255, 255);
    for (int i = 0; i < g->lives; i++) {
        draw_sprite8(r, SPR_SHIP, 8, WIN_W - 110 + i * 36, 24, 3, 100, 255, 100);
    }

    /* bottom line */
    SDL_SetRenderDrawColor(r, 80, 255, 80, 255);
    SDL_RenderDrawLine(r, 0, WIN_H - 40, WIN_W, WIN_H - 40);
}

static void render_title(SDL_Renderer *r)
{
    draw_string(r, "SPACE INVADERS", WIN_W/2 - 180, WIN_H/2 - 60, 3, 255, 255, 0);
    draw_string(r, "PRESS ENTER TO START", WIN_W/2 - 180, WIN_H/2 + 20, 2, 200, 200, 200);
    /* score table */
    int ty = WIN_H/2 + 70;
    draw_sprite8(r, SPR_A[0], 8, WIN_W/2 - 100, ty,      2, 80, 255, 80);
    draw_string(r, "= 30 PTS", WIN_W/2 - 70, ty,      2, 200, 200, 200);
    draw_sprite8(r, SPR_B[0], 8, WIN_W/2 - 100, ty + 24, 2, 80, 220, 255);
    draw_string(r, "= 20 PTS", WIN_W/2 - 70, ty + 24, 2, 200, 200, 200);
    draw_sprite8(r, SPR_C[0], 8, WIN_W/2 - 100, ty + 48, 2, 255, 255, 255);
    draw_string(r, "= 10 PTS", WIN_W/2 - 70, ty + 48, 2, 200, 200, 200);
    draw_sprite8(r, SPR_UFO,  6, WIN_W/2 - 100, ty + 72, 4, 255, 60,  60);
    draw_string(r, "= 150 PTS", WIN_W/2 - 70, ty + 72, 2, 200, 200, 200);
}

static void render_stage_clear(SDL_Renderer *r, const Game *g)
{
    Uint32 t = SDL_GetTicks();
    Uint8 bright = ((t / 200) & 1) ? 255 : 120;

    char buf[32];
    snprintf(buf, sizeof(buf), "STAGE %d CLEAR", g->stage);
    /* center: each char = (5+1)*scale = 18px at scale 3 */
    int len = (int)strlen(buf);
    draw_string(r, buf, WIN_W/2 - len * 9, WIN_H/2 - 30, 3, bright, bright, 0);

    if (g->stage < 6) {
        snprintf(buf, sizeof(buf), "STAGE %d", g->stage + 1);
        draw_string(r, "NEXT UP", WIN_W/2 - 42, WIN_H/2 + 30, 2, 200, 200, 200);
        draw_string(r, buf,       WIN_W/2 - (int)strlen(buf) * 6, WIN_H/2 + 52, 2, 255, 200, 0);
    }
}

static void render_game_over(SDL_Renderer *r, const Game *g)
{
    if (g->state == STATE_GAME_OVER) {
        draw_string(r, "GAME OVER", WIN_W/2 - 108, WIN_H/2 - 20, 3, 255, 40, 40);
        if (g->state_timer > 3.0f)
            draw_string(r, "PRESS ENTER", WIN_W/2 - 90, WIN_H/2 + 40, 2, 200, 200, 200);
    } else {
        draw_string(r, "ALL STAGES CLEAR", WIN_W/2 - 192, WIN_H/2 - 30, 3, 80, 255, 80);
        draw_string(r, "CONGRATULATIONS",  WIN_W/2 - 180, WIN_H/2 + 20, 2, 255, 255, 0);
        if (g->state_timer > 3.0f)
            draw_string(r, "PRESS ENTER", WIN_W/2 - 90, WIN_H/2 + 60, 2, 200, 200, 200);
    }
}

/* ── main render entry ─────────────────────────────────────────────────── */

void render_frame(SDL_Renderer *r, const Game *g)
{
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    if (g->state == STATE_TITLE) {
        render_title(r);
    } else {
        render_invaders(r, g);
        render_player(r, g);
        render_ufo(r, g);
        render_bullets(r, g);
        render_shields(r, g);
        render_hud(r, g);
        if (g->state == STATE_STAGE_CLEAR)
            render_stage_clear(r, g);
        if (g->state == STATE_GAME_OVER || g->state == STATE_WIN)
            render_game_over(r, g);
    }

    SDL_RenderPresent(r);
}
