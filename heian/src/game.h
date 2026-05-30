/* game.h – 平安京エイリアン 型定義・定数
 *
 * 【座標系】
 *   グリッド座標 (gx, gy): マップ上のタイル単位整数座標。(0,0) は左上。
 *   ピクセル座標 (px, py): SDL 描画用。px = GRID_OX + gx * TILE。
 *   移動中は move_t (0.0→1.0) で (gx,gy)→(tx,ty) を補間して px,py を計算し
 *   スムーズなアニメーションを実現する。
 *
 * 【マップ構造】
 *   道（TILE_PATH）だけをプレイヤー・エイリアンが歩ける。
 *   道の配置: row % STREET_STEP == 0 または col % STREET_STEP == 0 のセル。
 *   STREET_STEP=4 なので row=0,4,8,12 と col=0,4,8,12,16,20 が道の基本位置。
 *   道と道の間（3×3タイルの「街区」）に TILE_BLOCK を配置してブロックを形成。
 *
 * 【穴（TILE_HOLE）】
 *   道の上にのみ掘れる一時タイル。プレイヤーには壁として機能し通過不可。
 *   エイリアンは落下してトラップされる。FILL_NEEDED 回 F を押すと閉まる。
 *   HOLE_LIFETIME 秒後にエイリアンなしで自動消滅。
 */
#pragma once
#include <stdint.h>

/* ── ウィンドウ・グリッド定数 ───────────────────────────────────── */
#define WIN_W    800           /* ウィンドウ幅 (px) */
#define WIN_H    560           /* ウィンドウ高 (px) */
#define HUD_H    56            /* 上部 HUD の高さ (px) */
#define COLS     21            /* グリッド横幅（タイル数）: 道が col=0,4,8,12,16,20 に通る */
#define ROWS     13            /* グリッド縦幅（タイル数）: 道が row=0,4,8,12 に通る */
#define TILE     32            /* 1タイルのピクセルサイズ */
#define GRID_OX  64            /* グリッド左端の画面X座標 (800 - 21*32) / 2 = 64 */
#define GRID_OY  100           /* グリッド上端の画面Y座標 HUD_H + 余白 */

/* ── ゲームパラメータ ────────────────────────────────────────────── */
#define MAX_ALIENS    8        /* 最大エイリアン数（ステージ6で使用） */
#define MAX_STAGE     6        /* 全ステージ数 */
#define MOVE_TIME     0.12f   /* 1タイル移動に要する秒数（プレイヤー・エイリアン共通） */
#define HOLE_LIFETIME 12.0f   /* 穴が自動消滅するまでの秒数 */
#define CLIMB_TIME    8.0f    /* エイリアンが穴から脱出するまでの秒数 */
#define FILL_NEEDED   4        /* 穴を完全に埋めるのに必要な F キー押し回数 */

/* ── タイル種別 ─────────────────────────────────────────────────── */
typedef enum {
    TILE_PATH,    /* 道（街路）：プレイヤー・エイリアンが歩ける */
    TILE_BLOCK,   /* 街区ブロック：完全に通行不可（穴は掘れない） */
    TILE_HOLE,    /* 穴：道の上に掘られた一時タイル。
                   *   プレイヤーには壁扱い。
                   *   エイリアンは落下してトラップされる。
                   *   HOLE_LIFETIME 秒後または FILL_NEEDED 回埋めで TILE_PATH に戻る。 */
} TileType;

/* マップ生成ルール（init_grid 参照）:
 *   完全碁盤目（row%4==0 or col%4==0 は全て TILE_PATH）を基底として、
 *   内部の道路セグメントをランダムに除去して街区ブロックを不規則な形に変化させる。
 *   外周（row=0, row=12, col=0, col=20）は常に開通。 */
#define STREET_STEP 4   /* 道の基本間隔（タイル数）。4 なら 4 タイルごとに交差点 */

/* ── 方向 ───────────────────────────────────────────────────────── */
typedef enum { DIR_UP = 0, DIR_RIGHT, DIR_DOWN, DIR_LEFT } Dir;

/* 方向ごとの (dx, dy)。dy>0 が画面下方向（Y軸下向き） */
static const int DIR_DX[4] = { 0,  1,  0, -1 };
static const int DIR_DY[4] = {-1,  0,  1,  0 };

/* ── エンティティ（プレイヤー・エイリアン共用） ─────────────────── */
typedef struct {
    /* ── グリッド座標（整数、タイル単位） ── */
    int   gx, gy;       /* 現在いるタイルの座標 */
    int   tx, ty;       /* 移動先タイルの座標（移動中のみ gx/gy と異なる） */

    /* ── ピクセル座標（浮動小数、描画用） ── */
    float px, py;       /* 実際に描画する位置。gx/gy と tx/ty を move_t で補間 */

    /* ── 移動制御 ── */
    float move_t;       /* 移動進捗 0.0（出発）→1.0（到着）。毎フレーム dt/MOVE_TIME 加算 */
    int   moving;       /* 1=移動アニメーション中、0=停止中 */
    Dir   dir;          /* 現在向いている方向（スプライト選択・穴掘り方向に使用） */
    int   alive;        /* 0=死亡・非アクティブ */

    /* ── エイリアン専用フィールド ── */
    int   in_hole;      /* 1=穴に落ちてトラップ中 */
    float hole_timer;   /* 穴に入ってからの経過秒数。CLIMB_TIME を超えると脱出 */
    float move_timer;   /* 次の移動決定までのカウンタ（秒）。alien_move_interval で動く */
    int   ai_type;      /* AI 種別:
                         *   0=CHASER  直進追跡（プレイヤーへ最短経路）
                         *   1=FLANKER 側面回り込み（垂直方向から先に詰める）
                         *   2=AVOIDER 穴回避追跡（穴をよけながら追う）
                         *   3=WANDERER 変則（50%ランダム・50%追跡） */

    /* ── アニメーション ── */
    int   frame;        /* スプライトフレーム番号（0 or 1、0.25秒ごとに切り替え） */
    float anim_timer;   /* フレーム切り替えまでの経過秒数 */
} Entity;

/* ── ゲーム状態機械 ─────────────────────────────────────────────── */
typedef enum {
    STATE_TITLE,        /* タイトル画面。Enter でゲーム開始 */
    STATE_PLAYING,      /* 通常プレイ中 */
    STATE_DYING,        /* プレイヤー死亡演出中（2秒間点滅→残機減少） */
    STATE_STAGE_CLEAR,  /* ステージクリア演出中（3秒後に次ステージ or エンディング） */
    STATE_GAME_OVER,    /* ゲームオーバー画面 */
    STATE_WIN           /* 全ステージクリア画面 */
} GameState;

/* ── ゲーム全体の状態 ───────────────────────────────────────────── */
typedef struct {
    GameState state;
    int   stage;                        /* 現在ステージ番号（1〜MAX_STAGE） */
    int   score, hi_score, lives;

    /* 演出制御 */
    float state_timer;                  /* 現在の state に入ってからの経過秒数 */
    int   flash;                        /* STATE_DYING 中の点滅フラグ */
    int   kill_combo;                   /* 死なずに連続撃退した数（スコア倍率計算用） */

    /* エンティティ */
    Entity player;
    Entity aliens[MAX_ALIENS];
    int    alien_count;                 /* 今ステージの生存エイリアン総数 */
    float  alien_move_interval;         /* エイリアン移動間隔（秒）。ステージが上がるほど短縮 */

    /* マップ */
    TileType grid[ROWS][COLS];          /* タイルデータ。[row][col] でアクセス */
    float    hole_timers   [ROWS][COLS];/* TILE_HOLE の残り寿命（秒）。0以下で自動消滅 */
    int      hole_fill_cnt [ROWS][COLS];/* 穴への F キー累積回数。FILL_NEEDED で閉まる */

    /* 道路インデックス（stage_setup・respawn_player で使用） */
    int sr[ROWS]; int n_sr;  /* 横道の行番号リスト（row%4==0 の行）。sr[0]=0, sr[1]=4, … */
    int sc[COLS]; int n_sc;  /* 縦道の列番号リスト（col%4==0 の列）。sc[0]=0, sc[1]=4, … */

    /* just_pressed 判定用。前フレームのキー状態を保存して押し始めを検出 */
    uint8_t prev_keys[512];
} Game;

/* game.c */
void game_init(Game *g);
void game_update(Game *g, float dt, const uint8_t *keys);

/* render.c */
struct SDL_Renderer;
void render_frame(struct SDL_Renderer *r, const Game *g);
