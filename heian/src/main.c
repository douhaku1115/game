/* main.c – 平安京エイリアン エントリポイント
 *
 * 【ゲームループ構造】
 *   1. SDL イベント処理（ウィンドウ閉じ・ESC キー）
 *   2. デルタタイム計算: 前フレームからの経過時間を SDL_GetPerformanceCounter で計測。
 *      最大 0.05 秒にクランプすることでデバッガ停止・フォーカス外し時の
 *      大きなジャンプを防ぐ（60fps なら約 0.016 秒）。
 *   3. game_update: 入力・物理・AI を dt 秒分更新
 *   4. render_frame: 現在の状態を描画
 *   5. SDL_RENDERER_PRESENTVSYNC により垂直同期を自動で待つ
 */
#include "game.h"
#include "sound.h"
#include <SDL.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* 乱数シードを時刻で初期化（マップ生成のランダム性） */
    srand((unsigned)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "平安京エイリアン",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!win) { SDL_Quit(); return 1; }

    /* SDL_RENDERER_PRESENTVSYNC: SDL_RenderPresent が VSync まで待機する。
     * ゲームループに sleep を書かずに CPU を使いすぎない。 */
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { SDL_DestroyWindow(win); SDL_Quit(); return 1; }

    sound_init();

    Game g = {0};
    game_init(&g);

    Uint64 prev = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    int running = 1;

    while (running) {
        /* ── イベント処理 ── */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN &&
                ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = 0;
        }

        /* ── デルタタイム計算（最大 0.05 秒にクランプ） ── */
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)freq;
        if (dt > 0.05f) dt = 0.05f;
        prev = now;

        /* ── ゲーム更新 → 描画 ── */
        const uint8_t *keys = SDL_GetKeyboardState(NULL);
        game_update(&g, dt, keys);
        render_frame(ren, &g);
    }

    sound_quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
