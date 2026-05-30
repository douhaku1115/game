/* sound.h – 平安京エイリアン 効果音 ID */
#pragma once

typedef enum {
    SND_DIG,         /* 穴掘り */
    SND_FILL,        /* 穴埋め */
    SND_ALIEN_FALL,  /* エイリアンが穴に落下 */
    SND_ALIEN_DIE,   /* エイリアム撃退 */
    SND_PLAYER_DIE,  /* プレイヤー死亡 */
    SND_STAGE_CLEAR, /* ステージクリア */
    SND_MARCH,       /* エイリアム移動時のパルス音 */
    SND_COUNT
} SoundId;

void sound_init(void);
void sound_quit(void);
void sound_play(SoundId id);
