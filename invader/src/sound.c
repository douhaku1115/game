/* sound.c – サウンドエフェクト生成・再生
 *
 * SDL2 の低レイヤー Audio API を使い、外部ファイルなしで
 * 全効果音を PCM 波形合成で作成する。
 *
 * 設計の概要:
 *   - 起動時 (sound_init) に全効果音の波形バッファを生成してキャッシュ。
 *   - 再生はボイス（Voice）構造体のスロットを確保して行う。
 *   - SDL オーディオコールバック (audio_cb) が別スレッドで呼ばれ、
 *     全アクティブボイスを加算ミックスして出力する。
 */
#include "sound.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 44100   /* サンプリング周波数 (Hz) */
#define AMPLITUDE   16000   /* 基準振幅（最大 32767 の約半分。複数ボイス混合時のクリップ防止） */
#define MAX_VOICES  12      /* 同時再生できる最大ボイス数 */

/* 1つの再生中ボイス */
typedef struct {
    const int16_t *data;  /* 波形バッファ（sound_init で生成済みのもの） */
    int  len;             /* サンプル数 */
    int  pos;             /* 現在の再生位置（サンプル単位） */
    int  active;          /* 再生中フラグ */
} Voice;

static SDL_AudioDeviceID audio_dev;          /* SDL オーディオデバイスハンドル */
static Voice   voices[MAX_VOICES];           /* 同時再生ボイスのプール */
static int16_t *snd_bufs[SND_COUNT];         /* 効果音ごとの波形バッファ */
static int      snd_lens[SND_COUNT];         /* 効果音ごとのサンプル数 */

/* ── オーディオコールバック ────────────────────────────────────────
 * SDL が音声デバイスに書き込むタイミングで呼び出す（別スレッド）。
 * アクティブな全ボイスを加算ミックスして stream に書き込む。
 * int32_t で一時合計を取り int16_t にクランプすることでオーバーフローを防ぐ。
 * ──────────────────────────────────────────────────────────────── */
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out     = (int16_t *)stream;
    int      samples = len / (int)sizeof(int16_t);
    memset(stream, 0, len); /* まずバッファをゼロクリア（無音状態） */

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *vp = &voices[v];
        if (!vp->active) continue;
        for (int s = 0; s < samples; s++) {
            if (vp->pos >= vp->len) { vp->active = 0; break; } /* 再生終了 */
            int32_t m = (int32_t)out[s] + vp->data[vp->pos++]; /* 加算ミックス */
            /* int16_t の範囲 [-32768, 32767] にクランプ */
            out[s] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
        }
    }
}

/* ── 波形生成ヘルパー ──────────────────────────────────────────────
 * 全関数ともメモリを malloc して返す。呼び出し側が free する責務を持つ。
 * 音量エンベロープは vol0（開始）→ vol1（終了）の線形補間。
 * ──────────────────────────────────────────────────────────────── */

/* 矩形波（square wave）を生成する。
 * 位相を毎サンプル freq/SAMPLE_RATE だけ進め、0.5 未満なら +1、以上なら -1
 * を出力することで、正確な周波数の矩形波を得る。 */
static int16_t *gen_square(float freq, float dur,
                            float vol0, float vol1, int *len_out)
{
    int n = (int)(SAMPLE_RATE * dur);
    int16_t *buf = (int16_t *)malloc(n * sizeof(int16_t));
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t   = (float)i / n;
        float vol = vol0 + (vol1 - vol0) * t; /* 線形エンベロープ */
        phase += freq / SAMPLE_RATE;
        if (phase >= 1.0f) phase -= 1.0f;
        buf[i] = (int16_t)((phase < 0.5f ? 1 : -1) * AMPLITUDE * vol);
    }
    *len_out = n;
    return buf;
}

/* 周波数スイープ（sweep）付き矩形波を生成する。
 * 再生時間に沿って周波数を f0 から f1 へ線形変化させる。
 * 位相蓄積は各サンプルの瞬時周波数で行うので連続的に滑らか。 */
static int16_t *gen_sweep(float f0, float f1, float dur,
                           float vol0, float vol1, int *len_out)
{
    int n = (int)(SAMPLE_RATE * dur);
    int16_t *buf = (int16_t *)malloc(n * sizeof(int16_t));
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t    = (float)i / n;
        float freq = f0 + (f1 - f0) * t; /* 瞬時周波数 */
        float vol  = vol0 + (vol1 - vol0) * t;
        phase += freq / SAMPLE_RATE;
        if (phase >= 1.0f) phase -= 1.0f;
        buf[i] = (int16_t)((phase < 0.5f ? 1 : -1) * AMPLITUDE * vol);
    }
    *len_out = n;
    return buf;
}

/* ホワイトノイズを生成する。
 * rand() の出力を [-32768, 32767] にマッピングし 0.5 倍して音量調整。 */
static int16_t *gen_noise(float dur, float vol0, float vol1, int *len_out)
{
    int n = (int)(SAMPLE_RATE * dur);
    int16_t *buf = (int16_t *)malloc(n * sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        float t   = (float)i / n;
        float vol = vol0 + (vol1 - vol0) * t;
        buf[i] = (int16_t)((rand() % 65536 - 32768) * vol * 0.5f);
    }
    *len_out = n;
    return buf;
}

/* 2つのバッファを加算ミックスする（dst に src を上書きではなく加算）。
 * int32_t 経由でオーバーフローを防ぐ。 */
static void mix_into(int16_t *dst, const int16_t *src, int n)
{
    for (int i = 0; i < n; i++) {
        int32_t v = (int32_t)dst[i] + src[i];
        dst[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

/* 複数のバッファを時系列に連結して1つのバッファにする。
 * ステージクリアの4音アルペジオ生成に使用。 */
static int16_t *concat(int16_t **parts, int *lens, int count, int *len_out)
{
    int total = 0;
    for (int i = 0; i < count; i++) total += lens[i];
    int16_t *buf = (int16_t *)malloc(total * sizeof(int16_t));
    int pos = 0;
    for (int i = 0; i < count; i++) {
        memcpy(buf + pos, parts[i], lens[i] * sizeof(int16_t));
        pos += lens[i];
    }
    *len_out = total;
    return buf;
}

/* ── 公開 API ──────────────────────────────────────────────────── */

/* SDL オーディオデバイスを開き、全効果音の波形バッファを生成する。
 * 波形はすべて起動時に一括生成（プリキャッシュ）される。
 * 各効果音の仕様:
 *   SND_SHOOT      900Hz→400Hz スイープ、0.09秒（ビュン）
 *   SND_INV_DIE    ノイズ爆発、0.22秒（ドン）
 *   SND_PLAYER_DIE 440Hz→55Hz スイープ＋ノイズ混合、0.75秒（ズドーン）
 *   SND_UFO_HIT    200Hz→1400Hz 上昇スイープ、0.30秒（ピュイーン）
 *   SND_STAGE_CLEAR C-E-G-C アルペジオ、各0.11秒×4音
 *   SND_MARCH_0-3  160/130/100/80Hz 短パルス（行進音サイクル）
 */
void sound_init(void)
{
    SDL_AudioSpec want = {0}, got;
    want.freq     = SAMPLE_RATE;
    want.format   = AUDIO_S16SYS; /* ネイティブバイトオーダーの 16bit 符号付き整数 */
    want.channels = 1;            /* モノラル */
    want.samples  = 512;          /* コールバック1回あたりのサンプル数（レイテンシと安定性のバランス） */
    want.callback = audio_cb;

    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!audio_dev) {
        SDL_Log("Audio open failed: %s", SDL_GetError());
        return;
    }

    /* SND_SHOOT – 発射音：高音から中音へ短く降下 */
    snd_bufs[SND_SHOOT] = gen_sweep(900, 400, 0.09f, 0.55f, 0.0f,
                                     &snd_lens[SND_SHOOT]);

    /* SND_INV_DIE – 撃墜音：ノイズバースト */
    snd_bufs[SND_INV_DIE] = gen_noise(0.22f, 0.9f, 0.0f,
                                       &snd_lens[SND_INV_DIE]);

    /* SND_PLAYER_DIE – プレイヤー死亡音：降下スイープとノイズを重ねる */
    {
        int l1, l2;
        int16_t *sweep = gen_sweep(440, 55, 0.75f, 0.7f, 0.0f, &l1);
        int16_t *noise = gen_noise(0.75f, 0.5f, 0.0f, &l2);
        int n = l1 < l2 ? l1 : l2; /* 短い方の長さに合わせてミックス */
        mix_into(sweep, noise, n);
        free(noise);
        snd_bufs[SND_PLAYER_DIE] = sweep;
        snd_lens[SND_PLAYER_DIE] = n;
    }

    /* SND_UFO_HIT – UFO撃墜音：低音から高音へ上昇 */
    snd_bufs[SND_UFO_HIT] = gen_sweep(200, 1400, 0.30f, 0.65f, 0.0f,
                                       &snd_lens[SND_UFO_HIT]);

    /* SND_STAGE_CLEAR – ステージクリア音：C4-E4-G4-C5 の上昇アルペジオ */
    {
        static const float notes[4] = {261.6f, 329.6f, 392.0f, 523.3f};
        int16_t *parts[4];
        int      plens[4];
        for (int i = 0; i < 4; i++)
            parts[i] = gen_square(notes[i], 0.11f, 0.55f, 0.0f, &plens[i]);
        snd_bufs[SND_STAGE_CLEAR] = concat(parts, plens, 4,
                                            &snd_lens[SND_STAGE_CLEAR]);
        for (int i = 0; i < 4; i++) free(parts[i]);
    }

    /* SND_MARCH_0-3 – 行進音：4音サイクル（160/130/100/80 Hz） */
    static const float march_hz[4] = {160.f, 130.f, 100.f, 80.f};
    for (int i = 0; i < 4; i++)
        snd_bufs[SND_MARCH_0 + i] = gen_square(march_hz[i], 0.055f,
                                                 0.65f, 0.05f,
                                                 &snd_lens[SND_MARCH_0 + i]);

    SDL_PauseAudioDevice(audio_dev, 0); /* 再生開始（デフォルトは一時停止状態） */
}

/* SDL オーディオデバイスを閉じ、波形バッファを解放する */
void sound_quit(void)
{
    if (audio_dev) {
        SDL_CloseAudioDevice(audio_dev);
        audio_dev = 0;
    }
    for (int i = 0; i < SND_COUNT; i++) {
        free(snd_bufs[i]);
        snd_bufs[i] = NULL;
    }
}

/* 指定した効果音を再生する。
 * 空きボイススロットを探してバッファをセットする。
 * SDL_LockAudioDevice/UnlockAudioDevice でコールバックスレッドと
 * 排他制御を行い、race condition（中途半端な状態の読み取り）を防ぐ。 */
void sound_play(SoundId id)
{
    if (!audio_dev || id < 0 || id >= SND_COUNT) return;
    SDL_LockAudioDevice(audio_dev); /* コールバックスレッドを一時停止 */
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].active) {
            voices[v].data   = snd_bufs[id];
            voices[v].len    = snd_lens[id];
            voices[v].pos    = 0;
            voices[v].active = 1;
            break;
        }
    }
    SDL_UnlockAudioDevice(audio_dev); /* コールバックスレッドを再開 */
}
