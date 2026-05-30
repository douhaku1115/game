/* main.c – エントリポイントとゲームループ
 *
 * SDL2 の初期化→ウィンドウ・レンダラー生成→ゲームループ→終了処理
 * の流れを担う。ゲームロジックは game.c、描画は render.c、音声は sound.c。
 */
#include "game.h"
#include "sound.h"
#include <SDL.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    srand((unsigned)time(NULL)); /* 乱数初期化（インベーダーの発射列・UFO方向に使用） */

    /* SDL2 初期化（映像＋音声） */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    /* ウィンドウ作成 */
    SDL_Window *win = SDL_CreateWindow(
        "Space Invaders",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN
    );
    if (!win) {
        SDL_Log("CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* ハードウェアアクセラレーション＋垂直同期付きレンダラー */
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        SDL_Log("CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    /* サウンド初期化（PCM波形をここで生成・キャッシュする） */
    sound_init();

    /* ゲーム状態初期化 */
    Game g;
    game_init(&g);

    /* 高精度タイマーでデルタタイムを計測 */
    Uint64 prev = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    int running = 1;

    /* ── メインゲームループ ────────────────────────────────────── */
    while (running) {
        /* イベント処理（ウィンドウ閉じる / Esc で終了） */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN &&
                ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                running = 0;
        }

        /* デルタタイム計算
         * 上限を 0.05秒（20fps相当）に制限することで、ウィンドウが
         * フォーカスを失うなど処理が遅延した場合の「スパイラルオブデス」
         *（dt が大きい→物理更新も大きくずれる→さらに遅延）を防ぐ */
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)freq;
        if (dt > 0.05f) dt = 0.05f;
        prev = now;

        /* ゲーム更新・描画 */
        const uint8_t *keys = SDL_GetKeyboardState(NULL);
        game_update(&g, dt, keys);
        render_frame(ren, &g);
    }

    /* 終了処理 */
    sound_quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
