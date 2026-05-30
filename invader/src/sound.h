#pragma once

typedef enum {
    SND_SHOOT,
    SND_INV_DIE,
    SND_PLAYER_DIE,
    SND_UFO_HIT,
    SND_STAGE_CLEAR,
    SND_MARCH_0,
    SND_MARCH_1,
    SND_MARCH_2,
    SND_MARCH_3,
    SND_COUNT
} SoundId;

void sound_init(void);
void sound_quit(void);
void sound_play(SoundId id);
