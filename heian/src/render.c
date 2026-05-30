/* render.c – 平安京エイリアン 描画
 *
 * 【描画方針】
 *   SDL_RenderFillRect のみ使用（画像ファイル不使用）。
 *   スプライトは 8×8 ビットマップを draw_sprite8() で拡大描画する。
 *   1ビットが 1 ピクセルに対応し、TILE/8 倍（=4倍）に拡大してタイルに合わせる。
 *
 * 【スプライトビットマップ形式】
 *   uint8_t 配列の各要素が横1行に対応。ビット7(MSB)が左端、ビット0が右端。
 *   例: 0x18 = 0b00011000 → ...##... のように中央2ビットが ON。
 *
 * 【描画レイヤー順（render_frame）】
 *   1. グリッド（タイル背景）
 *   2. エイリアン
 *   3. プレイヤー
 *   4. HUD（スコア・残機・ステージ）
 *   5. オーバーレイ（クリア / ゲームオーバー）
 */
#include "game.h"
#include <SDL.h>
#include <string.h>
#include <stdio.h>

/* ── スプライト（8×8ビットマップ） ─────────────────────────────── */

/* プレイヤー（僧侶）4方向
 * DIR_UP/RIGHT/DOWN/LEFT の順でインデックスが対応する */
static const uint8_t SPR_PLAYER[4][8] = {
    /* DIR_UP: 袈裟を着た僧が上を向いた姿 */
    { 0x18, 0x18, 0x7E, 0x7E, 0x3C, 0x24, 0x66, 0x42 },
    /* DIR_RIGHT: 右を向いた横顔 */
    { 0x18, 0x3C, 0x7C, 0x7E, 0x7C, 0x38, 0x4C, 0x86 },
    /* DIR_DOWN: 下を向いた後ろ姿 */
    { 0x18, 0x18, 0x7E, 0x7E, 0x3C, 0x24, 0x24, 0x66 },
    /* DIR_LEFT: 左を向いた横顔（RIGHT の左右反転） */
    { 0x18, 0x3C, 0x3E, 0x7E, 0x3E, 0x1C, 0x32, 0x61 },
};

/* エイリアン（宇宙人）2フレームアニメーション
 * 0.25 秒ごとに frame 0↔1 を交互に表示して歩行感を出す */
static const uint8_t SPR_ALIEN[2][8] = {
    { 0x3C, 0xFF, 0xDB, 0xFF, 0x7E, 0x24, 0x42, 0x81 }, /* frame 0: 足を開いた状態 */
    { 0x3C, 0xFF, 0xDB, 0xFF, 0x7E, 0x42, 0x24, 0x81 }, /* frame 1: 足を閉じた状態 */
};

/* 穴（楕円形のシルエット）
 * 道に掘った穴を上から見た形で表現する */
static const uint8_t SPR_HOLE[8] = {
    0x00, 0x3C, 0x7E, 0x7E, 0x3C, 0x00, 0x00, 0x00
};

/* ── ビットマップフォント（5×7ドット）─────────────────────────── */
/* ASCII コードをインデックスとして使用。定義のない文字は 0x00 で空白扱い。
 * draw_string() で文字間を 1 ピクセル開けて描く（1文字幅 = (5+1)*scale px）。 */
static const uint8_t FONT5[128][7] = {
    ['0']={ 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E },
    ['1']={ 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E },
    ['2']={ 0x0E,0x11,0x01,0x06,0x08,0x10,0x1F },
    ['3']={ 0x0E,0x11,0x01,0x06,0x01,0x11,0x0E },
    ['4']={ 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 },
    ['5']={ 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E },
    ['6']={ 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E },
    ['7']={ 0x1F,0x01,0x02,0x04,0x08,0x08,0x08 },
    ['8']={ 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E },
    ['9']={ 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C },
    ['A']={ 0x04,0x0A,0x11,0x11,0x1F,0x11,0x11 },
    ['C']={ 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E },
    ['E']={ 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F },
    ['G']={ 0x0E,0x11,0x10,0x17,0x11,0x11,0x0E },
    ['H']={ 0x11,0x11,0x11,0x1F,0x11,0x11,0x11 },
    ['I']={ 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E },
    ['L']={ 0x10,0x10,0x10,0x10,0x10,0x10,0x1F },
    ['N']={ 0x11,0x19,0x15,0x13,0x11,0x11,0x11 },
    ['O']={ 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E },
    ['P']={ 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10 },
    ['R']={ 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11 },
    ['S']={ 0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E },
    ['T']={ 0x1F,0x04,0x04,0x04,0x04,0x04,0x04 },
    ['U']={ 0x11,0x11,0x11,0x11,0x11,0x11,0x0E },
    ['V']={ 0x11,0x11,0x11,0x11,0x0A,0x0A,0x04 },
    ['W']={ 0x11,0x11,0x11,0x15,0x15,0x1B,0x11 },
    ['X']={ 0x11,0x0A,0x04,0x04,0x04,0x0A,0x11 },
    ['Y']={ 0x11,0x0A,0x04,0x04,0x04,0x04,0x04 },
    ['-']={ 0x00,0x00,0x00,0x1F,0x00,0x00,0x00 },
    [' ']={ 0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
};

/* ── 描画ユーティリティ ──────────────────────────────────────────── */

/* 8×nrows ビットマップを (x,y) に scale 倍で描画する。
 * rows[i] のビット7が左端（x 座標が小さい）、ビット0が右端。
 * ON ビットを scale×scale のピクセルブロックとして描く。 */
static void draw_sprite8(SDL_Renderer *r, const uint8_t *rows, int nrows,
                          int x, int y, int scale, Uint8 R, Uint8 G, Uint8 B)
{
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    for (int row = 0; row < nrows; row++)
        for (int bit = 7; bit >= 0; bit--)
            if (rows[row] & (1 << bit)) {
                SDL_Rect rc = { x + (7-bit)*scale, y + row*scale, scale, scale };
                SDL_RenderFillRect(r, &rc);
            }
}

/* FONT5 から1文字を描画する（5×7 ドット、scale 倍） */
static void draw_char(SDL_Renderer *r, char c, int x, int y, int scale,
                       Uint8 R, Uint8 G, Uint8 B)
{
    if ((unsigned char)c >= 128) return;
    draw_sprite8(r, FONT5[(unsigned char)c], 7, x, y, scale, R, G, B);
}

/* 文字列を描画する。文字間は 1 ドット空ける（1文字分 = (5+1)*scale px）。 */
static void draw_string(SDL_Renderer *r, const char *s, int x, int y,
                         int scale, Uint8 R, Uint8 G, Uint8 B)
{
    for (; *s; s++, x += (5+1)*scale)
        draw_char(r, *s, x, y, scale, R, G, B);
}

/* 数値を digits 桁ゼロ埋めで描画する */
static void draw_number(SDL_Renderer *r, int n, int x, int y, int digits,
                         int scale, Uint8 R, Uint8 G, Uint8 B)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%0*d", digits, n);
    draw_string(r, buf, x, y, scale, R, G, B);
}

/* ── ゲーム画面の各パーツ描画 ───────────────────────────────────── */

/* グリッド（マップ全体）を描画する。
 * TILE_PATH: 暗い地面色。
 * TILE_BLOCK: 土色＋3D風の縁取り（上/左が明るく、下/右が暗い）。
 * TILE_HOLE:  黒楕円のスプライト＋埋め進捗バー＋残り時間警告枠。 */
static void render_grid(SDL_Renderer *r, const Game *g)
{
    /* まず背景全体を道の色で塗る */
    SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
    SDL_Rect bg = { GRID_OX, GRID_OY, COLS * TILE, ROWS * TILE };
    SDL_RenderFillRect(r, &bg);

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int x = GRID_OX + col * TILE;
            int y = GRID_OY + row * TILE;
            SDL_Rect rc = { x, y, TILE, TILE };

            switch (g->grid[row][col]) {
            case TILE_BLOCK:
                /* 街区ブロック：土色の塗りつぶし＋ハイライト */
                SDL_SetRenderDrawColor(r, 160, 110, 40, 255);
                SDL_RenderFillRect(r, &rc);
                /* 上・左端に明るい縁（光源が左上にある想定） */
                SDL_SetRenderDrawColor(r, 200, 155, 70, 255);
                SDL_RenderDrawLine(r, x, y, x+TILE-1, y);
                SDL_RenderDrawLine(r, x, y, x, y+TILE-1);
                /* 下・右端に暗い縁（影） */
                SDL_SetRenderDrawColor(r, 100, 68, 20, 255);
                SDL_RenderDrawLine(r, x+TILE-1, y, x+TILE-1, y+TILE-1);
                SDL_RenderDrawLine(r, x, y+TILE-1, x+TILE-1, y+TILE-1);
                break;

            case TILE_PATH:
                /* 道：暗い地面色（背景と同色） */
                SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
                SDL_RenderFillRect(r, &rc);
                break;

            case TILE_HOLE: {
                /* 穴：道の上に黒い楕円を描く */
                SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
                SDL_RenderFillRect(r, &rc);

                /* 埋め進捗バー: fill_cnt/FILL_NEEDED を高さに変換して
                 * 穴の下から土が積み上がるように見せる */
                int cnt = g->hole_fill_cnt[row][col];
                if (cnt > 0) {
                    int fill_h = (TILE * cnt) / FILL_NEEDED;
                    SDL_Rect fill_rc = { x, y + TILE - fill_h, TILE, fill_h };
                    SDL_SetRenderDrawColor(r, 120, 80, 30, 180);
                    SDL_RenderFillRect(r, &fill_rc);
                }

                /* 穴スプライト（黒い楕円）: TILE/8 = 4 倍スケール */
                int sc = TILE / 8;
                draw_sprite8(r, SPR_HOLE, 8, x, y, sc, 0, 0, 0);

                /* 残り時間 3 秒未満でオレンジの警告枠を表示 */
                float timer = g->hole_timers[row][col];
                Uint8 edge = (timer < 3.0f) ? 255 : 160;
                SDL_SetRenderDrawColor(r, edge, edge/3, 0, 255);
                SDL_RenderDrawRect(r, &rc);
                break;
            }
            }
        }
    }

    /* 道タイルの境界に薄いグリッド線を引いて交差点を見やすくする */
    SDL_SetRenderDrawColor(r, 60, 60, 60, 255);
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            if (g->grid[row][col] == TILE_PATH) {
                int x = GRID_OX + col * TILE;
                int y = GRID_OY + row * TILE;
                SDL_RenderDrawLine(r, x, y, x+TILE-1, y);
                SDL_RenderDrawLine(r, x, y, x, y+TILE-1);
            }
        }
    }
}

/* プレイヤーを描画する。
 * STATE_DYING 中は flash フラグに合わせて点滅させる。
 * スプライトは方向に応じて SPR_PLAYER[dir] を使用。 */
static void render_player(SDL_Renderer *r, const Game *g)
{
    if (!g->player.alive) return;
    if (g->state == STATE_DYING && g->flash) return;
    int x = (int)g->player.px;
    int y = (int)g->player.py;
    int sc = TILE / 8;  /* 32/8 = 4 倍スケール */
    draw_sprite8(r, SPR_PLAYER[g->player.dir], 8, x, y, sc, 255, 230, 100);
}

/* エイリアンを描画する。
 * 穴に落ちている間は縮小（半スケール）・暗い色で表示して「はまっている」感を出す。
 * 通常時は 2 フレームアニメーション（frame 0/1 交互）。 */
static void render_aliens(SDL_Renderer *r, const Game *g)
{
    for (int i = 0; i < MAX_ALIENS; i++) {
        const Entity *a = &g->aliens[i];
        if (!a->alive) continue;
        int x = (int)a->px;
        int y = (int)a->py;
        int sc = TILE / 8;

        if (a->in_hole) {
            /* 穴の中: タイル中央に半分サイズで描く */
            draw_sprite8(r, SPR_ALIEN[a->frame], 8,
                         x + TILE/4, y + TILE/4, sc/2 < 1 ? 1 : sc/2,
                         80, 180, 80);
        } else {
            draw_sprite8(r, SPR_ALIEN[a->frame], 8, x, y, sc, 80, 255, 80);
        }
    }
}

/* HUD（ヘッドアップディスプレイ）を描画する。
 * 配置: 左=SCORE、中=HI-SCORE、右=ST-N / LIVES */
static void render_hud(SDL_Renderer *r, const Game *g)
{
    /* 背景: 濃紺 */
    SDL_SetRenderDrawColor(r, 0, 0, 30, 255);
    SDL_Rect hud = { 0, 0, WIN_W, HUD_H };
    SDL_RenderFillRect(r, &hud);

    draw_string(r, "SCORE",    10,  8, 2, 255,255,255);
    draw_number(r, g->score,   10, 24, 5, 2, 255,255, 0);

    draw_string(r, "HI",      WIN_W/2 - 36,  8, 2, 255,255,255);
    draw_number(r, g->hi_score, WIN_W/2 - 36, 24, 5, 2, 255,255, 0);

    /* ステージ番号 */
    char sbuf[16];
    snprintf(sbuf, sizeof(sbuf), "ST-%d", g->stage);
    draw_string(r, sbuf, WIN_W - 200, 8, 2, 255,200, 0);

    /* 残機: プレイヤースプライトを lives 個並べて表示 */
    draw_string(r, "LIVES", WIN_W - 130, 8, 2, 255,255,255);
    for (int i = 0; i < g->lives; i++)
        draw_sprite8(r, SPR_PLAYER[DIR_UP], 8,
                     WIN_W - 128 + i * 20, 24, 2, 255,230,100);

    /* HUD 下端の区切り線 */
    SDL_SetRenderDrawColor(r, 80, 120, 80, 255);
    SDL_RenderDrawLine(r, 0, HUD_H - 2, WIN_W, HUD_H - 2);
}

/* タイトル画面を描画する */
static void render_title(SDL_Renderer *r)
{
    draw_string(r, "HEIAN ALIEN",
                WIN_W/2 - 132, WIN_H/2 - 60, 3, 255, 200, 0);
    draw_string(r, "PRESS ENTER",
                WIN_W/2 - 110, WIN_H/2 + 10, 2, 200, 200, 200);
    draw_string(r, "ARROW MOVE  SPACE DIG  F FILL",
                WIN_W/2 - 198, WIN_H/2 + 50, 2, 160, 200, 160);
}

/* ステージクリア / ゲームオーバー / クリア時のオーバーレイを描画する。
 * 半透明の黒で画面を暗くしてからテキストを重ねる。 */
static void render_overlay(SDL_Renderer *r, const Game *g)
{
    /* 半透明黒オーバーレイ（アルファ 160 / 255） */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_Rect ov = { 0, HUD_H, WIN_W, WIN_H - HUD_H };
    SDL_RenderFillRect(r, &ov);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    if (g->state == STATE_STAGE_CLEAR) {
        /* 点滅テキスト: SDL_GetTicks() を 200ms 周期で 0/1 に変換 */
        Uint32 t = SDL_GetTicks();
        Uint8 bright = ((t / 200) & 1) ? 255 : 120;
        char buf[32];
        snprintf(buf, sizeof(buf), "STAGE %d CLEAR", g->stage);
        draw_string(r, buf, WIN_W/2 - (int)strlen(buf)*9,
                    WIN_H/2 - 20, 3, bright, bright, 0);
        if (g->stage < MAX_STAGE) {
            snprintf(buf, sizeof(buf), "NEXT STAGE %d", g->stage + 1);
            draw_string(r, buf, WIN_W/2 - (int)strlen(buf)*6,
                        WIN_H/2 + 30, 2, 200, 200, 200);
        }
    } else if (g->state == STATE_GAME_OVER) {
        draw_string(r, "GAME OVER",
                    WIN_W/2 - 108, WIN_H/2 - 20, 3, 255, 40, 40);
        if (g->state_timer > 3.0f)
            draw_string(r, "PRESS ENTER",
                        WIN_W/2 - 90, WIN_H/2 + 40, 2, 200,200,200);
    } else if (g->state == STATE_WIN) {
        draw_string(r, "ALL CLEAR",
                    WIN_W/2 - 108, WIN_H/2 - 30, 3, 80, 255, 80);
        draw_string(r, "CONGRATULATIONS",
                    WIN_W/2 - 180, WIN_H/2 + 20, 2, 255,255, 0);
        if (g->state_timer > 3.0f)
            draw_string(r, "PRESS ENTER",
                        WIN_W/2 - 90, WIN_H/2 + 60, 2, 200,200,200);
    }
}

/* ── メイン描画エントリ ─────────────────────────────────────────── */
/* 毎フレーム呼ばれる。ゲーム状態に応じて必要なパーツだけ描画する。 */
void render_frame(SDL_Renderer *r, const Game *g)
{
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    if (g->state == STATE_TITLE) {
        render_title(r);
    } else {
        render_grid(r, g);    /* 1. マップ */
        render_aliens(r, g);  /* 2. エイリアン（プレイヤーより下のレイヤー） */
        render_player(r, g);  /* 3. プレイヤー */
        render_hud(r, g);     /* 4. HUD */
        /* クリア/ゲームオーバー/エンディング時は追加オーバーレイ */
        if (g->state == STATE_STAGE_CLEAR ||
            g->state == STATE_GAME_OVER   ||
            g->state == STATE_WIN)
            render_overlay(r, g);
    }

    SDL_RenderPresent(r);
}
