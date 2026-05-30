/* sound.c – PCM 波形合成サウンドエンジン
 *
 * 【設計方針】
 *   外部音声ファイルを一切使用せず、起動時に C コードで波形を生成してメモリに保持する。
 *   SDL の非同期オーディオコールバック（audio_cb）で複数音声を同時再生するソフトミキサー。
 *
 * 【PCM 合成の仕組み】
 *   サンプルレート 44100Hz、符号付き 16bit モノラル。
 *   1サンプル = 1/44100 秒に対応する -32768〜32767 の整数。
 *
 *   gen_square : 矩形波（デジタルチップ音）
 *     ・位相が 0.0→1.0 を繰り返す。位相 < 0.5 で +A、≥ 0.5 で -A。
 *     ・v0→v1 で音量フェード。
 *
 *   gen_sweep  : 周波数スイープの矩形波
 *     ・時刻 t に応じて周波数を f0→f1 に線形変化させる。
 *     ・落下音・上昇音などの効果に使用。
 *
 *   gen_noise  : ホワイトノイズ
 *     ・rand() で乱数サンプルを生成し音量フェードを乗算。
 *     ・掘る音・爆発音などに使用。
 *
 *   mix_into   : 2 つのバッファをサンプル単位で加算合成（クリップ付き）。
 *   concat     : 複数バッファを時系列に結合（アルペジオ等に使用）。
 *
 * 【ボイスミキサー】
 *   MAX_VOICES=12 スロットを用意。sound_play() で空きスロットに波形を割り当て、
 *   audio_cb でアクティブなスロットをすべて加算して出力する。
 *   再生中の音は pos（再生位置）が len（長さ）に達すると自動的に停止する。
 */
#include "sound.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 44100   /* サンプリング周波数 (Hz) */
#define AMPLITUDE   16000   /* 最大振幅 (0〜32767 の範囲で適度な音量) */
#define MAX_VOICES  12      /* 同時発音数の上限 */

/* 1つの発音スロット */
typedef struct {
    const int16_t *data;   /* 波形データへのポインタ（snd_bufs[] を参照） */
    int  len;              /* サンプル数 */
    int  pos;              /* 現在の再生位置（サンプル単位） */
    int  active;           /* 1=再生中、0=空きスロット */
} Voice;

static SDL_AudioDeviceID audio_dev;
static Voice   voices[MAX_VOICES];
static int16_t *snd_bufs[SND_COUNT];  /* サウンドごとの波形バッファ */
static int      snd_lens[SND_COUNT];  /* サウンドごとのサンプル数 */

/* SDL オーディオコールバック: オーディオスレッドから定期的に呼ばれる。
 * アクティブな全ボイスを加算合成して stream に書き込む。
 * 整数オーバーフロー防止のため 32bit で計算後 16bit にクリップする。 */
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int samples  = len / (int)sizeof(int16_t);
    memset(stream, 0, len);  /* まず無音で初期化 */
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *vp = &voices[v];
        if (!vp->active) continue;
        for (int s = 0; s < samples; s++) {
            if (vp->pos >= vp->len) { vp->active = 0; break; }
            int32_t m = (int32_t)out[s] + vp->data[vp->pos++];
            out[s] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
        }
    }
}

/* 矩形波を生成する。
 * freq: 周波数 (Hz)、dur: 長さ (秒)、v0→v1: 音量フェード (0.0〜1.0)。
 * 戻り値は malloc したバッファ（呼び出し元が free すること）。 */
static int16_t *gen_square(float freq, float dur, float v0, float v1, int *n_out)
{
    int n = (int)(SAMPLE_RATE * dur);
    int16_t *buf = malloc(n * sizeof(int16_t));
    float phase = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float vol = v0 + (v1 - v0) * t;           /* 時刻 t で音量を補間 */
        phase += freq / SAMPLE_RATE;               /* 1サンプル分の位相進み */
        if (phase >= 1.0f) phase -= 1.0f;
        buf[i] = (int16_t)((phase < 0.5f ? 1 : -1) * AMPLITUDE * vol);
    }
    *n_out = n; return buf;
}

/* 周波数スイープの矩形波を生成する。
 * f0→f1 に周波数を線形変化させながら矩形波を出力する。
 * 落下音（500Hz→100Hz）や上昇音（100Hz→500Hz）に使用。 */
static int16_t *gen_sweep(float f0, float f1, float dur, float v0, float v1, int *n_out)
{
    int n = (int)(SAMPLE_RATE * dur);
    int16_t *buf = malloc(n * sizeof(int16_t));
    float phase = 0;
    for (int i = 0; i < n; i++) {
        float t    = (float)i / n;
        float freq = f0 + (f1 - f0) * t;  /* 現在の周波数 */
        float vol  = v0 + (v1 - v0) * t;
        phase += freq / SAMPLE_RATE;
        if (phase >= 1.0f) phase -= 1.0f;
        buf[i] = (int16_t)((phase < 0.5f ? 1 : -1) * AMPLITUDE * vol);
    }
    *n_out = n; return buf;
}

/* ホワイトノイズを生成する。
 * rand() の出力を [-32768, 32767] にマッピングして vol を乗算。
 * 0.5 係数は音量抑制（フル振幅だとうるさすぎるため）。 */
static int16_t *gen_noise(float dur, float v0, float v1, int *n_out)
{
    int n = (int)(SAMPLE_RATE * dur);
    int16_t *buf = malloc(n * sizeof(int16_t));
    for (int i = 0; i < n; i++) {
        float vol = v0 + (v1 - v0) * (float)i / n;
        buf[i] = (int16_t)((rand() % 65536 - 32768) * vol * 0.5f);
    }
    *n_out = n; return buf;
}

/* 2 つの波形バッファを加算合成する（dst に src を上書き加算、クリップ付き）。
 * SND_PLAYER_DIE でスイープ音とノイズ音を同時鳴らすために使用。 */
static void mix_into(int16_t *dst, const int16_t *src, int n)
{
    for (int i = 0; i < n; i++) {
        int32_t v = (int32_t)dst[i] + src[i];
        dst[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

/* 複数の波形バッファを時系列に連結して1つのバッファにする。
 * SND_STAGE_CLEAR のアルペジオ（C-E-G-C）生成に使用。 */
static int16_t *concat(int16_t **parts, int *lens, int cnt, int *n_out)
{
    int total = 0;
    for (int i = 0; i < cnt; i++) total += lens[i];
    int16_t *buf = malloc(total * sizeof(int16_t));
    int pos = 0;
    for (int i = 0; i < cnt; i++) {
        memcpy(buf + pos, parts[i], lens[i] * sizeof(int16_t));
        pos += lens[i];
    }
    *n_out = total; return buf;
}

/* ── サウンドシステム初期化 ─────────────────────────────────────── */
/* オーディオデバイスを開き、全サウンドの波形を事前生成してメモリに格納する。
 * ゲーム中は snd_bufs[] を参照するだけなので実行時の生成コストはゼロ。 */
void sound_init(void)
{
    SDL_AudioSpec want = {0}, got;
    want.freq = SAMPLE_RATE; want.format = AUDIO_S16SYS;
    want.channels = 1; want.samples = 512; want.callback = audio_cb;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!audio_dev) { SDL_Log("Audio open failed: %s", SDL_GetError()); return; }

    /* SND_DIG – 穴を掘る「ザクッ」: 短いノイズバースト */
    snd_bufs[SND_DIG] = gen_noise(0.12f, 0.8f, 0.0f, &snd_lens[SND_DIG]);

    /* SND_FILL – 穴を埋める「ドサッ」: 高音→低音スイープ */
    snd_bufs[SND_FILL] = gen_sweep(300, 80, 0.20f, 0.7f, 0.0f, &snd_lens[SND_FILL]);

    /* SND_ALIEN_FALL – エイリアン落下「ヒュッ」: 中音→低音スイープ */
    snd_bufs[SND_ALIEN_FALL] = gen_sweep(500, 100, 0.30f, 0.6f, 0.0f,
                                          &snd_lens[SND_ALIEN_FALL]);

    /* SND_ALIEN_DIE – エイリアン撃退「ドン」: ノイズバースト */
    snd_bufs[SND_ALIEN_DIE] = gen_noise(0.25f, 0.9f, 0.0f, &snd_lens[SND_ALIEN_DIE]);

    /* SND_PLAYER_DIE – プレイヤー死亡「ズドーン」: 降下スイープ＋ノイズを mix_into で合成 */
    {
        int l1, l2;
        int16_t *sw = gen_sweep(400, 50, 0.70f, 0.7f, 0.0f, &l1);
        int16_t *ns = gen_noise(0.70f, 0.5f, 0.0f, &l2);
        int n = l1 < l2 ? l1 : l2;  /* 短い方の長さに合わせる */
        mix_into(sw, ns, n);         /* sw バッファにノイズを加算 */
        free(ns);
        snd_bufs[SND_PLAYER_DIE] = sw;
        snd_lens[SND_PLAYER_DIE] = n;
    }

    /* SND_STAGE_CLEAR – C-E-G-C アルペジオ: 4音符を concat で連結
     * ド(261.6Hz) ミ(329.6Hz) ソ(392.0Hz) 高ド(523.3Hz) */
    {
        static const float notes[4] = {261.6f, 329.6f, 392.0f, 523.3f};
        int16_t *parts[4]; int plens[4];
        for (int i = 0; i < 4; i++)
            parts[i] = gen_square(notes[i], 0.11f, 0.55f, 0.0f, &plens[i]);
        snd_bufs[SND_STAGE_CLEAR] = concat(parts, plens, 4, &snd_lens[SND_STAGE_CLEAR]);
        for (int i = 0; i < 4; i++) free(parts[i]);
    }

    /* SND_MARCH – エイリアン移動パルス: 低音矩形波の短いパルス（120Hz、50ms） */
    snd_bufs[SND_MARCH] = gen_square(120.f, 0.05f, 0.5f, 0.05f, &snd_lens[SND_MARCH]);

    SDL_PauseAudioDevice(audio_dev, 0);  /* 再生開始（デフォルトは一時停止状態） */
}

/* オーディオデバイスを閉じ、波形バッファを解放する */
void sound_quit(void)
{
    if (audio_dev) { SDL_CloseAudioDevice(audio_dev); audio_dev = 0; }
    for (int i = 0; i < SND_COUNT; i++) { free(snd_bufs[i]); snd_bufs[i] = NULL; }
}

/* 指定サウンドを再生する。
 * 空きボイスを探して割り当てる。全スロットが使用中なら無視（音が鳴らないが問題なし）。
 * SDL_LockAudioDevice でオーディオコールバックスレッドとの競合を防ぐ。 */
void sound_play(SoundId id)
{
    if (!audio_dev || id < 0 || id >= SND_COUNT) return;
    SDL_LockAudioDevice(audio_dev);
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].active) {
            voices[v].data = snd_bufs[id]; voices[v].len = snd_lens[id];
            voices[v].pos  = 0;            voices[v].active = 1;
            break;
        }
    }
    SDL_UnlockAudioDevice(audio_dev);
}
