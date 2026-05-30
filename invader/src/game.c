/* game.c – ゲームロジック全体
 *
 * 担当する処理:
 *   - ステージ初期化（インベーダー配置・難易度パラメータ設定）
 *   - プレイヤーの移動・発射
 *   - インベーダーの集団移動・発射・アニメーション
 *   - 弾とエンティティの当たり判定
 *   - シールド（盾）の破壊処理
 *   - UFO のスポーン・移動
 *   - ゲーム状態遷移（プレイ中→死亡→ステージクリア→ゲームオーバーなど）
 */
#include "game.h"
#include "sound.h"
#include <string.h>
#include <stdlib.h>
#include <SDL.h>

/* ── ステージ別難易度テーブル（ステージ1が最も易しい）─────────────── */
#define MAX_STAGE 6

/* インベーダー移動ステップ間隔（秒）。小さいほど速い */
static const float STAGE_STEP_INTERVAL[MAX_STAGE] = {0.80f,0.65f,0.52f,0.42f,0.34f,0.28f};

/* インベーダー弾の速度（px/秒） */
static const float STAGE_IBULLET_SPD  [MAX_STAGE] = {200.f,230.f,260.f,295.f,330.f,370.f};

/* UFO 出現間隔（秒）。小さいほど頻繁に出現 */
static const float STAGE_UFO_INTERVAL [MAX_STAGE] = {25.f, 22.f, 19.f, 16.f, 13.f, 10.f};

/* インベーダーが発射するタイミングのしきい値（秒）。小さいほど高頻度 */
static const float STAGE_SHOOT_THRESH [MAX_STAGE] = {0.80f,0.70f,0.60f,0.50f,0.42f,0.35f};

/* グリッド全体の初期Y方向オフセット（px）。高ステージほど下から始まる */
static const int   STAGE_INV_Y_OFFSET [MAX_STAGE] = {0, 16, 32, 48, 64, 80};

/* ── シールドのビットマップテンプレート ──────────────────────────── */
/* 各行は uint32_t の下位 SHIELD_COLS(22) ビットを使用。
 * bit0 = 左端のピクセル、bit21 = 右端のピクセル。
 * 1=ブロック存在、0=空白。実際の描画は 1ブロック = 4×4px。
 * 下段中央をくり抜いてバンカー（掩蔽壕）形状にしている。 */
static const uint32_t SHIELD_TEMPLATE[SHIELD_ROWS] = {
    0b0000111111111111111100000,
    0b0001111111111111111110000,
    0b0011111111111111111111000,
    0b0111111111111111111111100,
    0b1111111111111111111111110,
    0b1111111111111111111111110,
    0b1111111111111111111111110,
    0b1111111111111111111111110,
    0b1111111111111111111111110,
    0b1111111111111111111111110,
    0b1111110000000000001111110,  /* ← くり抜き開始 */
    0b1111100000000000000111110,
    0b1111000000000000000011110,
    0b1110000000000000000001110,
    0b1100000000000000000000110,
    0b1000000000000000000000010,
};

/* 全シールドをテンプレートで初期化する */
static void init_shields(Game *g)
{
    for (int s = 0; s < SHIELD_COUNT; s++)
        for (int r = 0; r < SHIELD_ROWS; r++)
            g->shields[s][r] = SHIELD_TEMPLATE[r];
}

/* ── ステージセットアップ ─────────────────────────────────────────── */
/* game_init() とは異なり、スコア・残機・ハイスコアは変更しない。
 * インベーダー・弾・UFO・シールドをリセットし、ステージに応じた
 * 難易度パラメータを Game 構造体に書き込む。 */
static void stage_setup(Game *g)
{
    int s = g->stage - 1; /* テーブルは 0 始まりなので -1 */

    /* インベーダーグリッドのリセット */
    g->inv_dir           = 1;                          /* 最初は右向きに移動 */
    g->inv_step_interval = STAGE_STEP_INTERVAL[s];
    g->inv_step_timer    = 0;
    g->inv_shoot_timer   = 0;
    g->inv_count         = INV_ROWS * INV_COLS;        /* 55体からスタート */
    g->inv_ox            = 0;
    g->inv_oy            = (float)STAGE_INV_Y_OFFSET[s]; /* 高ステージほど下から */
    g->inv_anim          = 0;

    /* ステージ依存パラメータ */
    g->ibullet_spd       = STAGE_IBULLET_SPD[s];
    g->inv_shoot_thresh  = STAGE_SHOOT_THRESH[s];
    g->ufo_timer         = STAGE_UFO_INTERVAL[s];

    /* プレイヤーをリセット（位置・弾のみ。残機はリセットしない） */
    g->player_x          = WIN_W / 2.0f;
    g->player_alive      = 1;
    g->shoot_cd          = 0;
    g->pbullet.active    = 0;
    g->ufo.active        = 0;
    for (int i = 0; i < MAX_INV_BULLETS; i++) g->ibullets[i].active = 0;

    /* インベーダーを 5行×11列 に配置 */
    for (int r = 0; r < INV_ROWS; r++) {
        /* 行によって種類（得点）を変える: 0行目=30pt, 1-2行目=20pt, 3-4行目=10pt */
        int type = (r == 0) ? 0 : (r <= 2) ? 1 : 2;
        for (int c = 0; c < INV_COLS; c++) {
            g->inv[r][c].x     = INV_ORIGIN_X + c * INV_SPACING_X;
            g->inv[r][c].y     = INV_ORIGIN_Y + r * INV_SPACING_Y;
            g->inv[r][c].alive = 1;
            g->inv[r][c].type  = type;
            g->inv[r][c].frame = 0;
        }
    }
    init_shields(g);
}

/* ── ゲーム全体の初期化（ステージ1からリスタート） ─────────────────── */
void game_init(Game *g)
{
    int hi = g->hi_score;   /* ハイスコアだけは引き継ぐ */
    memset(g, 0, sizeof(*g));
    g->hi_score = hi;
    g->state    = STATE_TITLE;
    g->lives    = 3;
    g->stage    = 1;
    stage_setup(g);
}

/* ── インベーダー移動速度の更新 ────────────────────────────────────── */
/* インベーダーを1体倒すたびに呼ぶ。
 * 通常: ステージ基準値から 0.10秒 まで線形に短縮していく。
 * 残り少数になると段階的に強制上限を適用し、劇的な加速を演出する。 */
static void update_inv_speed(Game *g)
{
    float base = STAGE_STEP_INTERVAL[g->stage - 1]; /* ステージ開始時の間隔 */
    int   n    = g->inv_count;

    /* 線形補間: n=55 → base秒、n=1 → 0.10秒 */
    float t = 0.10f + (base - 0.10f) * n / (float)(INV_ROWS * INV_COLS);

    /* 残り機数ごとの強制上限（下限値）。これより遅くならないようにキャップ */
    if (n <= 4) t = (t < 0.15f) ? t : 0.15f;
    if (n <= 3) t = (t < 0.11f) ? t : 0.11f;
    if (n <= 2) t = (t < 0.08f) ? t : 0.08f;
    if (n <= 1) t = (t < 0.05f) ? t : 0.05f;

    g->inv_step_interval = t;
}

/* ── ユーティリティ ──────────────────────────────────────────────── */

/* 2つの矩形が重なっているか判定（AABB衝突） */
static int rect_hit(float ax, float ay, int aw, int ah,
                    float bx, float by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx &&
           ay < by + bh && ay + ah > by;
}

/* 生存しているインベーダーの左端・右端X座標を返す */
static void inv_extents(const Game *g, float *lx, float *rx)
{
    *lx = 1e9f; *rx = -1e9f;
    for (int r = 0; r < INV_ROWS; r++)
        for (int c = 0; c < INV_COLS; c++)
            if (g->inv[r][c].alive) {
                float x = g->inv[r][c].x + g->inv_ox;
                if (x       < *lx) *lx = x;
                if (x+INV_W > *rx) *rx = x + INV_W;
            }
}

/* 生存しているインベーダーの最も低いY座標（下辺）を返す。
 * プレイヤーラインに到達したらゲームオーバー判定に使う。 */
static float inv_bottom(const Game *g)
{
    float bot = 0;
    for (int r = 0; r < INV_ROWS; r++)
        for (int c = 0; c < INV_COLS; c++)
            if (g->inv[r][c].alive) {
                float y = g->inv[r][c].y + g->inv_oy + INV_H;
                if (y > bot) bot = y;
            }
    return bot;
}

/* インベーダーが1発の弾を発射する。
 * Fisher-Yates シャッフルで列の順番をランダム化し、
 * 各列の最下段の生存インベーダーから発射させることで
 * 「手前の敵を盾にして後ろから撃ってくる」効果を避けつつ
 * ランダム性を持たせている。 */
static void inv_shoot(Game *g)
{
    /* 空きスロットを探す */
    int slot = -1;
    for (int i = 0; i < MAX_INV_BULLETS; i++)
        if (!g->ibullets[i].active) { slot = i; break; }
    if (slot < 0) return; /* 全スロット使用中なら発射しない */

    /* 列インデックスを Fisher-Yates でシャッフル */
    int cols[INV_COLS];
    for (int i = 0; i < INV_COLS; i++) cols[i] = i;
    for (int i = INV_COLS - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = cols[i]; cols[i] = cols[j]; cols[j] = t;
    }

    /* シャッフル後の順に列を見て、最下段の生存インベーダーから発射 */
    for (int ci = 0; ci < INV_COLS; ci++) {
        int c = cols[ci];
        for (int r = INV_ROWS - 1; r >= 0; r--) {
            if (g->inv[r][c].alive) {
                float ix = g->inv[r][c].x + g->inv_ox + INV_W / 2.0f;
                float iy = g->inv[r][c].y + g->inv_oy + INV_H;
                g->ibullets[slot].x      = ix - BULLET_W / 2.0f;
                g->ibullets[slot].y      = iy;
                g->ibullets[slot].active = 1;
                g->ibullets[slot].dir    = 1; /* 下向き */
                return;
            }
        }
    }
}

/* シールドの指定ピクセル付近（半径2px）のブロックを消去する。
 * 弾が当たった中心座標を渡すと、5×5 の範囲のビットを一括クリアする。 */
static void shield_damage(Game *g, float px, float py)
{
    /* 各シールドの画面左端X座標と上端Y座標 */
    int sx0[SHIELD_COUNT] = {80, 240, 400, 560};
    int sy0 = WIN_H - 160;

    for (int s = 0; s < SHIELD_COUNT; s++) {
        /* シールドローカル座標（ピクセル単位ではなくブロック単位） */
        int lx = (int)(px - sx0[s]);
        int ly = (int)(py - sy0);
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                int col = lx + dx;
                int row = ly + dy;
                /* 範囲チェックしてビットをクリア */
                if (col >= 0 && col < SHIELD_COLS &&
                    row >= 0 && row < SHIELD_ROWS)
                    g->shields[s][row] &= ~(1u << col);
            }
    }
}

/* 弾がシールドに当たっているか判定し、当たっていれば破壊して 1 を返す。
 * ビットマップを行・列方向に走査し、存在するブロックとの矩形衝突を確認。
 * 1ブロック = 4px なので、座標変換して rect_hit に渡す。 */
static int bullet_hits_shield(Game *g, float bx, float by, int bw, int bh)
{
    int sx0[SHIELD_COUNT] = {80, 240, 400, 560};
    int sy0 = WIN_H - 160;
    int pw = 4; /* 1ブロックの実際のピクセルサイズ */

    for (int s = 0; s < SHIELD_COUNT; s++) {
        for (int row = 0; row < SHIELD_ROWS; row++) {
            if (!g->shields[s][row]) continue; /* この行に生きているブロックがなければスキップ */
            for (int col = 0; col < SHIELD_COLS; col++) {
                if (!(g->shields[s][row] & (1u << col))) continue;
                /* ブロックのワールド座標 */
                float px = sx0[s] + col * pw;
                float py = sy0    + row * pw;
                if (rect_hit(bx, by, bw, bh, px, py, pw, pw)) {
                    shield_damage(g, px + pw / 2.0f, py + pw / 2.0f);
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* ── メイン更新関数（毎フレーム呼び出し） ─────────────────────────── */
void game_update(Game *g, float dt, const uint8_t *keys)
{
    /* ── タイトル画面 ── */
    if (g->state == STATE_TITLE) {
        if (keys[SDL_SCANCODE_RETURN]) {
            game_init(g);
            g->state = STATE_PLAYING;
        }
        return;
    }

    /* ── ステージクリア演出（3秒待って次ステージへ） ── */
    if (g->state == STATE_STAGE_CLEAR) {
        g->state_timer += dt;
        if (g->state_timer > 3.0f) {
            if (g->stage >= MAX_STAGE) {
                /* 全ステージクリア → エンディング */
                g->state = STATE_WIN;
                g->state_timer = 0;
            } else {
                /* 次のステージへ（スコア・残機は引き継ぎ） */
                g->stage++;
                stage_setup(g);
                g->state = STATE_PLAYING;
            }
        }
        return;
    }

    /* ── ゲームオーバー / エンディング（Enter で最初からリスタート） ── */
    if (g->state == STATE_GAME_OVER || g->state == STATE_WIN) {
        g->state_timer += dt;
        if (g->state_timer > 3.0f && keys[SDL_SCANCODE_RETURN]) {
            game_init(g);
            g->state = STATE_PLAYING;
        }
        return;
    }

    /* ── 死亡演出（約2秒点滅、その後残機減算） ── */
    if (g->state == STATE_DYING) {
        g->state_timer += dt;
        /* 8Hz で点滅: timer * 8 の整数部の LSB が 0/1 を繰り返す */
        g->flash = (int)(g->state_timer * 8) & 1;
        if (g->state_timer > 2.0f) {
            g->lives--;
            if (g->lives <= 0) {
                g->state = STATE_GAME_OVER;
                g->state_timer = 0;
            } else {
                /* 残機があれば復活 */
                g->player_x       = WIN_W / 2.0f;
                g->player_alive   = 1;
                g->pbullet.active = 0;
                g->state          = STATE_PLAYING;
            }
        }
        return;
    }

    /* ── プレイヤー移動・発射 ── */
    if (g->player_alive) {
        if (keys[SDL_SCANCODE_LEFT])
            g->player_x -= PLAYER_SPEED * dt;
        if (keys[SDL_SCANCODE_RIGHT])
            g->player_x += PLAYER_SPEED * dt;
        /* 画面端でクランプ */
        if (g->player_x < PLAYER_W / 2.0f) g->player_x = PLAYER_W / 2.0f;
        if (g->player_x > WIN_W - PLAYER_W / 2.0f)
            g->player_x = WIN_W - PLAYER_W / 2.0f;

        /* スペースキーで発射（クールダウン中・弾が既にある場合は不可） */
        g->shoot_cd -= dt;
        if (keys[SDL_SCANCODE_SPACE] &&
            g->shoot_cd <= 0 && !g->pbullet.active) {
            g->pbullet.x      = g->player_x - BULLET_W / 2.0f;
            g->pbullet.y      = PLAYER_Y - PBULLET_H;
            g->pbullet.active = 1;
            g->pbullet.dir    = -1; /* 上向き */
            g->shoot_cd       = 0.4f;
            sound_play(SND_SHOOT);
        }
    }

    /* ── プレイヤー弾の移動・衝突 ── */
    if (g->pbullet.active) {
        g->pbullet.y += PBULLET_SPD * g->pbullet.dir * dt;
        if (g->pbullet.y < 0) { g->pbullet.active = 0; } /* 画面外に出たら消去 */

        /* UFO との衝突 */
        if (g->ufo.active) {
            if (rect_hit(g->pbullet.x, g->pbullet.y, BULLET_W, PBULLET_H,
                         g->ufo.x, g->ufo.y, UFO_W, UFO_H)) {
                g->score += UFO_SCORE;
                if (g->score > g->hi_score) g->hi_score = g->score;
                g->ufo.active     = 0;
                g->pbullet.active = 0;
                sound_play(SND_UFO_HIT);
            }
        }

        /* インベーダーとの衝突 */
        if (g->pbullet.active) {
            for (int r = 0; r < INV_ROWS && g->pbullet.active; r++) {
                for (int c = 0; c < INV_COLS && g->pbullet.active; c++) {
                    Invader *iv = &g->inv[r][c];
                    if (!iv->alive) continue;
                    /* グローバル座標に変換して判定 */
                    float ix = iv->x + g->inv_ox;
                    float iy = iv->y + g->inv_oy;
                    if (rect_hit(g->pbullet.x, g->pbullet.y, BULLET_W, PBULLET_H,
                                 ix, iy, INV_W, INV_H)) {
                        iv->alive = 0;
                        g->inv_count--;
                        int pts = (iv->type == 0) ? 30 :
                                  (iv->type == 1) ? 20 : 10;
                        g->score += pts;
                        sound_play(SND_INV_DIE);
                        if (g->score > g->hi_score) g->hi_score = g->score;
                        g->pbullet.active = 0;
                        update_inv_speed(g); /* 残り数に応じて速度を再計算 */
                    }
                }
            }
        }

        /* シールドとの衝突 */
        if (g->pbullet.active)
            if (bullet_hits_shield(g, g->pbullet.x, g->pbullet.y,
                                   BULLET_W, PBULLET_H))
                g->pbullet.active = 0;
    }

    /* ── インベーダー弾の移動・衝突 ── */
    for (int i = 0; i < MAX_INV_BULLETS; i++) {
        Bullet *b = &g->ibullets[i];
        if (!b->active) continue;
        b->y += g->ibullet_spd * dt; /* ステージ依存の速度で下へ */
        if (b->y > WIN_H) { b->active = 0; continue; } /* 画面外で消去 */

        /* プレイヤーとの衝突 */
        if (g->player_alive) {
            float px = g->player_x - PLAYER_W / 2.0f;
            if (rect_hit(b->x, b->y, BULLET_W, IBULLET_H,
                         px, PLAYER_Y, PLAYER_W, PLAYER_H)) {
                b->active       = 0;
                g->player_alive = 0;
                g->state        = STATE_DYING;
                g->state_timer  = 0;
                g->flash        = 0;
                sound_play(SND_PLAYER_DIE);
                return; /* この frame は以降の処理をスキップ */
            }
        }

        /* シールドとの衝突 */
        if (bullet_hits_shield(g, b->x, b->y, BULLET_W, IBULLET_H))
            b->active = 0;
    }

    /* ── インベーダーの移動ステップ ── */
    g->inv_step_timer += dt;
    if (g->inv_step_timer >= g->inv_step_interval) {
        g->inv_step_timer = 0;
        g->inv_anim ^= 1; /* アニメーションフレーム切り替え（0→1→0→…） */

        /* 行進音を 4音サイクルで鳴らす */
        sound_play(SND_MARCH_0 + g->march_step);
        g->march_step = (g->march_step + 1) & 3;

        /* 全生存インベーダーのアニメーションフレームを同期して更新 */
        for (int r = 0; r < INV_ROWS; r++)
            for (int c = 0; c < INV_COLS; c++)
                if (g->inv[r][c].alive)
                    g->inv[r][c].frame = g->inv_anim;

        /* グリッド全体を 6px 横移動 */
        float step = 6.0f;
        g->inv_ox += step * g->inv_dir;

        /* 端に達したら反転して下に降りる */
        float lx, rx;
        inv_extents(g, &lx, &rx);
        if (g->inv_dir == 1 && rx >= WIN_W - 10) {
            g->inv_dir = -1;
            g->inv_oy += INV_DROP;
        } else if (g->inv_dir == -1 && lx <= 10) {
            g->inv_dir = 1;
            g->inv_oy += INV_DROP;
        }

        /* インベーダーがプレイヤーラインまで到達 → ゲームオーバー */
        if (inv_bottom(g) >= PLAYER_Y) {
            g->state = STATE_GAME_OVER;
            g->state_timer = 0;
            return;
        }

        /* 移動ステップごとに発射タイマーを加算し、しきい値を超えたら発射 */
        g->inv_shoot_timer += g->inv_step_interval;
        if (g->inv_shoot_timer >= g->inv_shoot_thresh) {
            g->inv_shoot_timer = 0;
            inv_shoot(g);
        }
    }

    /* ── UFO の出現・移動 ── */
    g->ufo_timer -= dt;
    if (g->ufo_timer <= 0 && !g->ufo.active) {
        /* タイマー切れで UFO を画面端からスポーン */
        g->ufo.active = 1;
        g->ufo.dir    = (rand() & 1) ? 1 : -1; /* 左右どちらから来るかランダム */
        g->ufo.x      = (g->ufo.dir == 1) ? -UFO_W : WIN_W;
        g->ufo.y      = 30;
        g->ufo_timer  = UFO_INTERVAL;
    }
    if (g->ufo.active) {
        g->ufo.x += UFO_SPD * g->ufo.dir * dt;
        /* 画面外に出たら非表示に */
        if (g->ufo.x > WIN_W + UFO_W || g->ufo.x < -UFO_W * 2)
            g->ufo.active = 0;
    }

    /* ── ステージクリア判定 ── */
    if (g->inv_count <= 0) {
        g->state = STATE_STAGE_CLEAR;
        g->state_timer = 0;
        sound_play(SND_STAGE_CLEAR);
    }
}
