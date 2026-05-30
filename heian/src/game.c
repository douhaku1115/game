/* game.c – 平安京エイリアン ゲームロジック
 *
 * マップ構造:
 *   平安京（現在の京都）の碁盤目状の都市計画を再現。
 *   col % STREET_STEP == 0 または row % STREET_STEP == 0 の位置が「道（街路）」。
 *   それ以外は「街区ブロック」で完全に通行不可。
 *   プレイヤーもエイリアムも道の上しか歩けない。
 *   穴は道の上にのみ掘ることができる。
 */
#include "game.h"
#include "sound.h"
#include <string.h>
#include <stdlib.h>
#include <SDL.h>

/* ── ステージ難易度テーブル ─────────────────────────────────────── */
static const int   STAGE_ALIENS[MAX_STAGE] = {3, 4, 5, 6, 7, 8};
static const float STAGE_SPEED [MAX_STAGE] = {0.90f,0.82f,0.74f,0.66f,0.58f,0.50f};


/* ── グリッド初期化 ─────────────────────────────────────────────── */
/* アルゴリズム（完全グリッドからの制限付き削除）:
 *  1. 全道路セル PATH で初期化（完全碁盤目）。
 *  2. 横セグメント除去: ゾーン列ごとに最大1本（row4 か row8 のどちらか）。
 *     → 縦に最大2ゾーン分だけ合体。3ゾーン縦長は生まれない。
 *  3. 縦セグメント除去: ゾーン行ごとにシャッフルして隣接2本同時除去を禁止。
 *     → 横に最大2ゾーン分だけ合体。3ゾーン横長は生まれない。
 *  4. BFS で全交差点の連結確認。非連結なら再試行。
 * 結果: 道は1タイル幅・交差点が多く・ブロックは最大2ゾーン分の不規則な形。 */
static void init_grid(Game *g, int stage)
{
    (void)stage;

    g->n_sr = 0;
    for (int r = 0; r < ROWS; r += STREET_STEP) g->sr[g->n_sr++] = r;
    g->n_sc = 0;
    for (int c = 0; c < COLS; c += STREET_STEP) g->sc[g->n_sc++] = c;

    int ok = 0;
    while (!ok) {

        /* ── Step 1: 全道路セル PATH、ゾーンセル BLOCK ── */
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++)
                g->grid[r][c] = (r % STREET_STEP == 0 || c % STREET_STEP == 0)
                                ? TILE_PATH : TILE_BLOCK;

        /* ── Step 2: 横セグメント除去 ──
         * ゾーン列 (ic=0..n_sc-2) ごとに 50% の確率で1本だけ除去する。
         * row4 か row8 のどちらかをランダムに選ぶ（両方同時除去しない）。 */
        for (int ic = 0; ic < g->n_sc - 1; ic++) {
            if (rand() % 2 == 0) continue;
            int ir = 1 + rand() % (g->n_sr - 2);   /* ir=1(row4) or ir=2(row8) */
            for (int k = 1; k < STREET_STEP; k++)
                g->grid[g->sr[ir]][g->sc[ic] + k] = TILE_BLOCK;
        }

        /* ── Step 3: 縦セグメント除去 ──
         * ゾーン行 (ir=0..n_sr-2) ごとに内側の縦道 (ic=1..n_sc-2) をシャッフル。
         * 隣接する ic を同時に除去しない → 横方向も最大2ゾーン分の合体に限定。 */
        for (int ir = 0; ir < g->n_sr - 1; ir++) {
            int cands[4]; int nc = 0;
            for (int ic = 1; ic < g->n_sc - 1; ic++) cands[nc++] = ic;
            for (int i = nc - 1; i > 0; i--) {     /* Fisher-Yates */
                int j = rand() % (i + 1);
                int t = cands[i]; cands[i] = cands[j]; cands[j] = t;
            }
            int done[6] = {0};
            for (int i = 0; i < nc; i++) {
                int ic = cands[i];
                if (done[ic - 1] || done[ic + 1]) continue; /* 隣接除去禁止 */
                if (rand() % 2 == 0) {
                    for (int k = 1; k < STREET_STEP; k++)
                        g->grid[g->sr[ir] + k][g->sc[ic]] = TILE_BLOCK;
                    done[ic] = 1;
                }
            }
        }

        /* ── Step 4: BFS 連結性チェック ── */
        uint8_t vis[ROWS][COLS];
        memset(vis, 0, sizeof(vis));
        int que[ROWS * COLS][2]; int head = 0, tail = 0;
        vis[0][0] = 1;
        que[tail][0] = 0; que[tail][1] = 0; tail++;
        while (head < tail) {
            int r = que[head][0], c = que[head][1]; head++;
            for (int d = 0; d < 4; d++) {
                int nr = r + DIR_DY[d], nc = c + DIR_DX[d];
                if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS
                    && !vis[nr][nc] && g->grid[nr][nc] == TILE_PATH) {
                    vis[nr][nc] = 1;
                    que[tail][0] = nr; que[tail][1] = nc; tail++;
                }
            }
        }
        ok = 1;
        for (int ir = 0; ir < g->n_sr && ok; ir++)
            for (int ic = 0; ic < g->n_sc && ok; ic++)
                if (!vis[g->sr[ir]][g->sc[ic]]) ok = 0;
    }

    memset(g->hole_timers,   0, sizeof(g->hole_timers));
    memset(g->hole_fill_cnt, 0, sizeof(g->hole_fill_cnt));
}

/* ── エンティティのピクセル座標更新 ─────────────────────────────── */
static void update_px(Entity *e)
{
    float lx = e->gx + (e->tx - e->gx) * e->move_t;
    float ly = e->gy + (e->ty - e->gy) * e->move_t;
    e->px = GRID_OX + lx * TILE;
    e->py = GRID_OY + ly * TILE;
}

/* ── プレイヤーだけリスポーン（マップ・エイリアン状態は維持） ─────── */
static void respawn_player(Game *g)
{
    /* 残っている穴をすべて道に戻す（再死亡防止） */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (g->grid[r][c] == TILE_HOLE)
                g->grid[r][c] = TILE_PATH;
    memset(g->hole_timers,   0, sizeof(g->hole_timers));
    memset(g->hole_fill_cnt, 0, sizeof(g->hole_fill_cnt));

    /* プレイヤーを最下段中央交差点へ */
    int pr = g->sr[g->n_sr - 1];
    int pc = g->sc[0]; int best_d = abs(g->sc[0] - COLS / 2);
    for (int i = 1; i < g->n_sc; i++) {
        int d = abs(g->sc[i] - COLS / 2);
        if (d < best_d) { best_d = d; pc = g->sc[i]; }
    }
    g->player.gx = g->player.tx = pc;
    g->player.gy = g->player.ty = pr;
    g->player.px = GRID_OX + pc * TILE;
    g->player.py = GRID_OY + pr * TILE;
    g->player.dir    = DIR_UP;
    g->player.moving = 0; g->player.move_t = 0; g->player.alive = 1;
}

/* ── ステージセットアップ ────────────────────────────────────────── */
static void stage_setup(Game *g)
{
    int s = g->stage - 1;
    init_grid(g, g->stage);
    g->alien_move_interval = STAGE_SPEED[s];
    g->kill_combo = 0;

    /* プレイヤー: 最下段の道の行 × 中央に最も近い道の列の交差点 */
    int pr = g->sr[g->n_sr - 1]; /* 最下段の横道 */
    int pc = g->sc[0]; int best_d = abs(g->sc[0] - COLS/2);
    for (int i = 1; i < g->n_sc; i++) {
        int d = abs(g->sc[i] - COLS/2);
        if (d < best_d) { best_d = d; pc = g->sc[i]; }
    }
    g->player.gx = g->player.tx = pc;
    g->player.gy = g->player.ty = pr;
    g->player.px = GRID_OX + pc * TILE;
    g->player.py = GRID_OY + pr * TILE;
    g->player.dir    = DIR_UP;
    g->player.moving = 0; g->player.move_t = 0; g->player.alive = 1;

    /* エイリアム: 上側の交差点（道の行×道の列）に配置する。
     * sc[] の列を順に使い、行は上から2段分を使いまわす。
     * プレイヤーと同じ交差点を避ける。 */
    g->alien_count = STAGE_ALIENS[s];
    int n_top_rows = (g->n_sr >= 3) ? 2 : 1; /* 使う上段の道の行数 */
    for (int i = 0; i < MAX_ALIENS; i++) {
        Entity *a = &g->aliens[i];
        memset(a, 0, sizeof(*a));
        if (i < g->alien_count) {
            int ri = (i / g->n_sc) % n_top_rows; /* 行インデックス (0=最上段) */
            int ci = i % g->n_sc;                 /* 列インデックス */
            int agr = g->sr[ri];
            int agc = g->sc[ci];
            /* プレイヤーと同じ位置なら隣の列へ */
            if (agr == pr && agc == pc)
                agc = g->sc[(ci + 1) % g->n_sc];

            a->alive      = 1;
            a->gx = a->tx = agc;
            a->gy = a->ty = agr;
            a->px = GRID_OX + agc * TILE;
            a->py = GRID_OY + agr * TILE;
            a->dir        = DIR_DOWN;
            a->move_timer = (float)i * 0.20f;
            a->ai_type    = i % 4;
        }
    }
}

/* ── ゲーム全体の初期化 ─────────────────────────────────────────── */
void game_init(Game *g)
{
    int hi = g->hi_score;
    memset(g, 0, sizeof(*g));
    g->hi_score = hi;
    g->state    = STATE_TITLE;
    g->lives    = 3;
    g->stage    = 1;
    stage_setup(g);
}

/* ── ユーティリティ ─────────────────────────────────────────────── */

static int just_pressed(const Game *g, const uint8_t *keys, SDL_Scancode sc)
{
    return keys[sc] && !g->prev_keys[sc];
}

static void front_tile(const Entity *e, int *fx, int *fy)
{
    *fx = e->gx + DIR_DX[e->dir];
    *fy = e->gy + DIR_DY[e->dir];
}

/* 指定座標にトラップ中エイリアムがいれば index を返す（いなければ -1） */
static int find_alien_in_hole(const Game *g, int gx, int gy)
{
    for (int i = 0; i < MAX_ALIENS; i++) {
        const Entity *a = &g->aliens[i];
        if (a->alive && a->in_hole && a->gx == gx && a->gy == gy)
            return i;
    }
    return -1;
}

static int count_alive_aliens(const Game *g)
{
    int n = 0;
    for (int i = 0; i < MAX_ALIENS; i++)
        if (g->aliens[i].alive) n++;
    return n;
}

/* プレイヤーと穴に落ちていないエイリアムが接触しているか */
static int player_collides(const Game *g)
{
    for (int i = 0; i < MAX_ALIENS; i++) {
        const Entity *a = &g->aliens[i];
        if (!a->alive || a->in_hole) continue;
        if (a->gx == g->player.gx && a->gy == g->player.gy) return 1;
        if (g->player.moving &&
            a->gx == g->player.tx && a->gy == g->player.ty) return 1;
    }
    return 0;
}

/* 移動先タイルが歩行可能か（道 or 穴のみ。ブロックは不可。範囲外も不可） */
static int is_walkable(const Game *g, int c, int r)
{
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 0;
    return g->grid[r][c] == TILE_PATH || g->grid[r][c] == TILE_HOLE;
}

/* 指定方向に移動できるか。avoid_hole=1 のとき穴タイルも通行不可とする */
static int can_move(const Game *g, const Entity *a, int d, int avoid_hole)
{
    int nx = a->gx + DIR_DX[d];
    int ny = a->gy + DIR_DY[d];
    if (!is_walkable(g, nx, ny)) return 0;
    if (avoid_hole && g->grid[ny][nx] == TILE_HOLE) return 0;
    return 1;
}

/* エイリアムの移動方向を AI タイプ別に決定する。
 *
 *  ai_type 0 – 直進追跡（CHASER）
 *    プレイヤーへ最短経路。穴回避なし。同じ穴に集まりやすいが最も速い。
 *
 *  ai_type 1 – 側面回り込み（FLANKER）
 *    プレイヤーへの接近を横軸・縦軸の優先順を逆にして行う。
 *    直進型とは異なる経路から挟み込む。
 *
 *  ai_type 2 – 穴回避（AVOIDER）
 *    穴を前方に見つけたとき、そこを避けて迂回路を選ぶ。
 *    単純な穴トラップが効きにくい。
 *
 *  ai_type 3 – 変則（WANDERER）
 *    50% でランダム方向、50% でプレイヤー方向。
 *    予測しにくい動きで別方向から接近してくる。
 */
static int choose_alien_dir(const Game *g, const Entity *a)
{
    int pdx = g->player.gx - a->gx;
    int pdy = g->player.gy - a->gy;

    /* プレイヤー方向の軸候補 */
    int ph = (pdx > 0) ? DIR_RIGHT : (pdx < 0) ? DIR_LEFT : -1; /* 横軸優先方向 */
    int pv = (pdy > 0) ? DIR_DOWN  : (pdy < 0) ? DIR_UP   : -1; /* 縦軸優先方向 */

    /* ランダムシャッフル（フォールバック用） */
    int shuf[4] = {0,1,2,3};
    for (int i = 3; i > 0; i--) {
        int j = rand() % (i+1);
        int t = shuf[i]; shuf[i] = shuf[j]; shuf[j] = t;
    }

    /* AI タイプ別に優先方向リストを作成 */
    int prio[6]; int np = 0;
    int avoid = 0;

    switch (a->ai_type) {

    case 0: /* 直進追跡：距離が長い軸から接近 */
        if (abs(pdx) >= abs(pdy)) { prio[np++]=ph; prio[np++]=pv; }
        else                       { prio[np++]=pv; prio[np++]=ph; }
        avoid = 0;
        break;

    case 1: /* 側面回り込み：距離が短い軸（側面）から先に動く */
        if (abs(pdx) >= abs(pdy)) { prio[np++]=pv; prio[np++]=ph; } /* 横が長い→縦から回る */
        else                       { prio[np++]=ph; prio[np++]=pv; } /* 縦が長い→横から回る */
        avoid = 0;
        break;

    case 2: /* 穴回避：直進追跡と同じ優先軸だが穴を避ける */
        if (abs(pdx) >= abs(pdy)) { prio[np++]=ph; prio[np++]=pv; }
        else                       { prio[np++]=pv; prio[np++]=ph; }
        avoid = 1;
        break;

    case 3: /* 変則：50%ランダム、50%直進 */
        if (rand() & 1) {
            /* ランダム方向を優先リストに入れる */
            for (int i = 0; i < 4; i++) prio[np++] = shuf[i];
        } else {
            if (abs(pdx) >= abs(pdy)) { prio[np++]=ph; prio[np++]=pv; }
            else                       { prio[np++]=pv; prio[np++]=ph; }
        }
        avoid = 0;
        break;
    }

    /* 優先リストから最初に移動できる方向を返す */
    for (int i = 0; i < np; i++) {
        int d = prio[i];
        if (d >= 0 && can_move(g, a, d, avoid)) return d;
    }

    /* フォールバック：穴回避なしでランダム方向 */
    for (int i = 0; i < 4; i++) {
        if (can_move(g, a, shuf[i], 0)) return shuf[i];
    }

    return -1;
}

/* ── メイン更新 ──────────────────────────────────────────────────── */
void game_update(Game *g, float dt, const uint8_t *keys)
{
    if (g->state == STATE_TITLE) {
        if (just_pressed(g, keys, SDL_SCANCODE_RETURN)) {
            game_init(g);
            g->state = STATE_PLAYING;
        }
        goto done;
    }

    if (g->state == STATE_STAGE_CLEAR) {
        g->state_timer += dt;
        if (g->state_timer > 3.0f) {
            if (g->stage >= MAX_STAGE) {
                g->state = STATE_WIN; g->state_timer = 0;
            } else {
                g->stage++;
                stage_setup(g);
                g->state = STATE_PLAYING;
            }
        }
        goto done;
    }

    if (g->state == STATE_GAME_OVER || g->state == STATE_WIN) {
        g->state_timer += dt;
        if (g->state_timer > 3.0f && just_pressed(g, keys, SDL_SCANCODE_RETURN)) {
            game_init(g);
            g->state = STATE_PLAYING;
        }
        goto done;
    }

    if (g->state == STATE_DYING) {
        g->state_timer += dt;
        g->flash = (int)(g->state_timer * 8) & 1;
        if (g->state_timer > 2.0f) {
            g->lives--;
            g->kill_combo = 0;
            if (g->lives <= 0) {
                g->state = STATE_GAME_OVER; g->state_timer = 0;
            } else {
                respawn_player(g);   /* マップ・エイリアン維持でプレイヤーのみ復活 */
                g->state = STATE_PLAYING;
            }
        }
        goto done;
    }

    /* ════════ PLAYING ════════ */

    /* ── 穴を掘る（Space）: 向いている前方の道マスに穴を掘る ── */
    if (just_pressed(g, keys, SDL_SCANCODE_SPACE) && !g->player.moving) {
        int fx, fy; front_tile(&g->player, &fx, &fy);
        if (fx >= 0 && fx < COLS && fy >= 0 && fy < ROWS &&
            g->grid[fy][fx] == TILE_PATH) {    /* 道のみ掘れる（ブロックは不可） */
            g->grid[fy][fx]        = TILE_HOLE;
            g->hole_timers[fy][fx] = HOLE_LIFETIME;
            sound_play(SND_DIG);
        }
    }

    /* ── 穴を埋める（F）: 4回押しで完全に閉まる ── */
    if (just_pressed(g, keys, SDL_SCANCODE_F) && !g->player.moving) {
        int fx, fy; front_tile(&g->player, &fx, &fy);
        if (fx >= 0 && fx < COLS && fy >= 0 && fy < ROWS &&
            g->grid[fy][fx] == TILE_HOLE) {
            g->hole_fill_cnt[fy][fx]++;
            sound_play(SND_FILL);

            if (g->hole_fill_cnt[fy][fx] >= FILL_NEEDED) {
                /* FILL_NEEDED 回押し切った → 穴が完全に閉まる */
                int ai = find_alien_in_hole(g, fx, fy);
                if (ai >= 0) {
                    /* エイリアムを撃退 */
                    g->aliens[ai].alive = 0;
                    g->kill_combo++;
                    g->score += g->kill_combo * 100;
                    if (g->score > g->hi_score) g->hi_score = g->score;
                    sound_play(SND_ALIEN_DIE);
                }
                g->grid[fy][fx]          = TILE_PATH;
                g->hole_fill_cnt[fy][fx] = 0;
            }
        }
    }

    /* ── プレイヤー移動（矢印キー）: 道のみ移動可能 ── */
    if (!g->player.moving && g->player.alive) {
        Dir newdir = (Dir)-1;
        if      (keys[SDL_SCANCODE_UP])    newdir = DIR_UP;
        else if (keys[SDL_SCANCODE_DOWN])  newdir = DIR_DOWN;
        else if (keys[SDL_SCANCODE_LEFT])  newdir = DIR_LEFT;
        else if (keys[SDL_SCANCODE_RIGHT]) newdir = DIR_RIGHT;

        if ((int)newdir >= 0) {
            g->player.dir = newdir;
            int tx = g->player.gx + DIR_DX[newdir];
            int ty = g->player.gy + DIR_DY[newdir];

            /* 穴・ブロック・範囲外はすべて通行不可（穴は壁として機能する） */
            if (tx >= 0 && tx < COLS && ty >= 0 && ty < ROWS &&
                g->grid[ty][tx] == TILE_PATH) {
                g->player.tx = tx; g->player.ty = ty;
                g->player.moving = 1; g->player.move_t = 0;
            }
        }
    }

    /* 移動補間 */
    if (g->player.moving) {
        g->player.move_t += dt / MOVE_TIME;
        if (g->player.move_t >= 1.0f) {
            g->player.move_t = 1.0f;
            g->player.gx = g->player.tx;
            g->player.gy = g->player.ty;
            g->player.moving = 0;
        }
        update_px(&g->player);
    }

    /* ── エイリアム更新 ── */
    for (int i = 0; i < MAX_ALIENS; i++) {
        Entity *a = &g->aliens[i];
        if (!a->alive) continue;

        /* アニメーション */
        a->anim_timer += dt;
        if (a->anim_timer >= 0.25f) { a->frame ^= 1; a->anim_timer = 0; }

        /* 穴に落ちてよじ登り中 */
        if (a->in_hole) {
            a->hole_timer += dt;
            if (a->hole_timer >= CLIMB_TIME) {
                /* 隣接する道マスに脱出 */
                for (int d = 0; d < 4; d++) {
                    int nx = a->gx + DIR_DX[d];
                    int ny = a->gy + DIR_DY[d];
                    if (nx >= 0 && nx < COLS && ny >= 0 && ny < ROWS &&
                        g->grid[ny][nx] == TILE_PATH) {
                        g->grid[a->gy][a->gx]             = TILE_PATH;
                g->hole_fill_cnt[a->gy][a->gx]   = 0;
                        a->gx = a->tx = nx; a->gy = a->ty = ny;
                        a->px = GRID_OX + nx * TILE;
                        a->py = GRID_OY + ny * TILE;
                        a->in_hole = 0; a->moving = 0;
                        break;
                    }
                }
                if (a->in_hole) a->hole_timer = CLIMB_TIME - 0.3f;
            }
            continue;
        }

        /* 移動補間 */
        if (a->moving) {
            a->move_t += dt / MOVE_TIME;
            if (a->move_t >= 1.0f) {
                a->move_t = 1.0f;
                a->gx = a->tx; a->gy = a->ty;
                a->moving = 0;
            }
            update_px(a);
            continue;
        }

        /* 移動タイマー */
        a->move_timer += dt;
        if (a->move_timer < g->alien_move_interval) continue;
        a->move_timer = 0;

        int d = choose_alien_dir(g, a);
        if (d < 0) continue;

        int nx = a->gx + DIR_DX[d];
        int ny = a->gy + DIR_DY[d];
        a->dir = (Dir)d;

        if (g->grid[ny][nx] == TILE_HOLE) {
            /* 穴に落下 */
            a->gx = a->tx = nx; a->gy = a->ty = ny;
            a->px = GRID_OX + nx * TILE; a->py = GRID_OY + ny * TILE;
            a->in_hole = 1; a->hole_timer = 0; a->moving = 0;
            sound_play(SND_ALIEN_FALL);
        } else if (g->grid[ny][nx] == TILE_PATH) {
            /* 道を移動 */
            a->tx = nx; a->ty = ny;
            a->moving = 1; a->move_t = 0;
            sound_play(SND_MARCH);
        }
        /* TILE_BLOCK には移動しない（AI 側で弾いているが念のため） */
    }

    /* ── 穴の自動消滅（エイリアムのいない穴のみ） ── */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (g->grid[r][c] != TILE_HOLE) continue;
            if (find_alien_in_hole(g, c, r) >= 0) continue;
            g->hole_timers[r][c] -= dt;
            if (g->hole_timers[r][c] <= 0) {
                g->grid[r][c]         = TILE_PATH;
                g->hole_fill_cnt[r][c] = 0;
            }
        }
    }

    /* ── 衝突判定 ── */
    if (g->player.alive && player_collides(g)) {
        g->player.alive = 0;
        g->state = STATE_DYING; g->state_timer = 0; g->flash = 0;
        sound_play(SND_PLAYER_DIE);
        goto done;
    }

    /* ── ステージクリア ── */
    if (count_alive_aliens(g) == 0) {
        g->state = STATE_STAGE_CLEAR; g->state_timer = 0;
        sound_play(SND_STAGE_CLEAR);
    }

done:
    memcpy(g->prev_keys, keys, 512);
}
