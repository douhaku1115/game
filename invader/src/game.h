/* game.h – ゲーム全体の型定義・定数・関数プロトタイプ
 *
 * このヘッダを include すれば game.c / render.c どちらからでも
 * ゲーム状態 (Game) にアクセスできる。
 */
#pragma once
#include <stdint.h>

/* ── ウィンドウ ──────────────────────────────────────────────────── */
#define WIN_W        800          /* ウィンドウ幅 (px) */
#define WIN_H        600          /* ウィンドウ高さ (px) */

/* ── インベーダーグリッド ────────────────────────────────────────── */
#define INV_COLS     11           /* 横11列 */
#define INV_ROWS     5            /* 縦5行、合計55体 */
#define MAX_INV_BULLETS 3         /* 同時に存在できるインベーダー弾の最大数 */

/* ── シールド (盾) ───────────────────────────────────────────────── */
#define SHIELD_COUNT 4            /* 盾の個数 */
#define SHIELD_COLS  22           /* 1個の盾の横ピクセル数（ビット幅） */
#define SHIELD_ROWS  16           /* 1個の盾の縦ピクセル数 */

/* ── プレイヤー ─────────────────────────────────────────────────── */
#define PLAYER_W     36           /* プレイヤー機の当たり判定幅 (px) */
#define PLAYER_H     20           /* プレイヤー機の当たり判定高さ (px) */
#define PLAYER_SPEED 220.0f       /* 移動速度 (px/秒) */
#define PLAYER_Y     (WIN_H - 60) /* プレイヤーの固定Y座標 */

/* ── インベーダーのサイズ・配置 ──────────────────────────────────── */
#define INV_W        24           /* インベーダー1体の幅 (px) */
#define INV_H        24           /* インベーダー1体の高さ (px) */
#define INV_SPACING_X 48          /* 横方向の間隔 (中心間距離) */
#define INV_SPACING_Y 40          /* 縦方向の間隔 */
#define INV_ORIGIN_X  70          /* グリッド左上の初期X座標 */
#define INV_ORIGIN_Y  80          /* グリッド左上の初期Y座標 */
#define INV_DROP      16          /* 壁に当たるたびに下に降りる量 (px) */

/* ── 弾 ─────────────────────────────────────────────────────────── */
#define BULLET_W     3            /* 弾の幅 (px) */
#define PBULLET_H    12           /* プレイヤー弾の高さ (px) */
#define IBULLET_H    10           /* インベーダー弾の高さ (px) */
#define PBULLET_SPD  480.0f       /* プレイヤー弾の速度 (px/秒) */
#define IBULLET_SPD  200.0f       /* インベーダー弾の初期速度（ステージで上昇） */

/* ── UFO ────────────────────────────────────────────────────────── */
#define UFO_W        48           /* UFOの幅 (px) */
#define UFO_H        20           /* UFOの高さ (px) */
#define UFO_SPD      120.0f       /* UFOの移動速度 (px/秒) */
#define UFO_SCORE    150          /* UFO撃墜得点 */
#define UFO_INTERVAL 25.0f        /* UFO出現間隔の初期値 (秒) */

/* ── エンティティ構造体 ──────────────────────────────────────────── */

/* インベーダー1体 */
typedef struct {
    float x, y;    /* グリッド内のローカル座標（実際の位置は + inv_ox/oy） */
    int   alive;   /* 生存フラグ */
    int   type;    /* 種類: 0=上段(30pt)  1=中段(20pt)  2=下段(10pt) */
    int   frame;   /* アニメーションフレーム: 0 or 1 */
} Invader;

/* 弾（プレイヤー弾・インベーダー弾共用） */
typedef struct {
    float x, y;
    int   active;  /* 飛行中フラグ */
    int   dir;     /* 移動方向: -1=上(プレイヤー弾)  +1=下(インベーダー弾) */
} Bullet;

/* UFO */
typedef struct {
    float x, y;
    int   active;
    int   dir;     /* 移動方向: +1=右  -1=左 */
} UFO;

/* ── ゲーム状態 ──────────────────────────────────────────────────── */
typedef enum {
    STATE_TITLE,        /* タイトル画面 */
    STATE_PLAYING,      /* ゲームプレイ中 */
    STATE_DYING,        /* プレイヤー死亡演出中（点滅後に残機減算） */
    STATE_STAGE_CLEAR,  /* ステージクリア演出中（3秒後に次ステージへ） */
    STATE_GAME_OVER,    /* ゲームオーバー画面 */
    STATE_WIN           /* 全6ステージクリアのエンディング */
} GameState;

/* ── ゲーム全体の状態 ────────────────────────────────────────────── */
typedef struct {
    GameState state;

    /* プレイヤー */
    float player_x;       /* プレイヤーX座標（中心）。Y座標は PLAYER_Y 固定 */
    int   player_alive;   /* 生存フラグ（0になると STATE_DYING へ） */
    int   lives;          /* 残機数 */
    int   score;          /* 現在スコア */
    int   hi_score;       /* ハイスコア（ゲームリセットをまたいで保持） */
    float shoot_cd;       /* 発射クールダウン（秒）、0以下で発射可能 */

    /* インベーダーグリッド */
    Invader inv[INV_ROWS][INV_COLS];
    int     inv_count;          /* 生存しているインベーダーの数 */
    int     inv_dir;            /* グリッド全体の移動方向: +1=右  -1=左 */
    float   inv_step_timer;     /* 次の移動ステップまでの経過時間 */
    float   inv_step_interval;  /* 移動ステップ間隔（秒）、倒すほど短縮 */
    float   inv_shoot_timer;    /* 発射タイミング用タイマー */
    int     inv_anim;           /* アニメーションフレーム切り替え: 0 or 1 */

    /* グリッド全体のワールド座標オフセット
     * 各インベーダーの実際の描画位置 = inv[r][c].x + inv_ox, .y + inv_oy
     * 左右移動・下降はこのオフセットを動かすことで全体をまとめて移動させる */
    float   inv_ox;
    float   inv_oy;

    /* 弾 */
    Bullet  pbullet;               /* プレイヤー弾（同時1発のみ） */
    Bullet  ibullets[MAX_INV_BULLETS]; /* インベーダー弾（同時3発まで） */

    /* UFO */
    UFO     ufo;
    float   ufo_timer;  /* 次にUFOが出現するまでのカウントダウン（秒） */

    /* シールド（盾）
     * shields[s][row] の下位 SHIELD_COLS ビットが各ピクセルの生存状態。
     * bit0 = 左端、bit21 = 右端。1=ブロック存在、0=破壊済み。
     * 1ブロック = 4×4 px で描画される。 */
    uint32_t shields[SHIELD_COUNT][SHIELD_ROWS];

    /* 状態遷移用タイマー・演出 */
    float state_timer;   /* 各状態に入ってからの経過時間（秒） */
    int   flash;         /* 死亡演出で点滅させるフラグ（8Hz で 0/1 切り替え） */

    /* ステージシステム */
    int   stage;              /* 現在のステージ番号: 1〜6 */
    float ibullet_spd;        /* インベーダー弾速度（ステージが上がるほど速くなる） */
    float inv_shoot_thresh;   /* 発射タイマーのしきい値（小さいほど高頻度） */
    int   march_step;         /* 行進音の再生位置: 0〜3 をサイクル */
} Game;

/* ── 関数プロトタイプ ────────────────────────────────────────────── */

/* game.c */
void game_init(Game *g);                               /* ゲーム全体の初期化（ステージ1から） */
void game_update(Game *g, float dt, const uint8_t *keys); /* 毎フレーム呼ぶ更新関数 */

/* render.c */
struct SDL_Renderer;
void render_frame(struct SDL_Renderer *r, const Game *g); /* 画面全体を1フレーム描画 */
