/*
 * sfx.c — PSG sound engine (SN76489 direct).
 *
 * Channels: 0/1 = one-shot sfx, 2 = ambient (wind / radio static), 3 = noise.
 * All sound is generated with SGDK PSG_* register helpers; sound effects are
 * small step sequences (tone+volume per frame). PLACEHOLDER audio — final
 * game uses FM (YM2612) music + PCM ambience, see README.
 */
#include "game.h"

#define CH_A 0
#define CH_B 1
#define CH_AMB 2
#define CH_NOISE 3

/* ambient modes */
#define AMB_NONE  0
#define AMB_WIND  1
#define AMB_RADIO 2

/* sfx step: wait frames, then set (channel-independent) state */
typedef struct {
    u8 wait;        /* frames to wait before applying */
    u8 vol;         /* attenuation 0..15 (15=silent) */
    u16 tone;       /* 12-bit PSG tone value (111860/f) */
    u8 noiseMode;   /* 0 = tone mode, 1 = white noise, 2 = periodic */
    u8 noiseFreq;   /* 0..3 */
} SfxStep;

/* max steps per sfx */
#define MAX_STEPS 12

typedef struct {
    const SfxStep *steps;
    u8 len;
} SfxDef;

/* ---- step tables (hand-tuned; PLACEHOLDER sound design) ---- */
/* pickup: bright two-tone chime */
static const SfxStep stPickup[] = {
    {0,  6, 96,  0, 0},     /* ~1165Hz */
    {4,  6, 68,  0, 0},     /* ~1645Hz */
    {6, 10, 68,  0, 0},
    {6, 15, 0,   0, 0},
};
/* knife swing: noise + quick falling tone */
static const SfxStep stKnife[] = {
    {0,  8, 340, 1, 3},
    {2,  6, 220, 0, 0},
    {4,  8, 140, 0, 0},
    {5, 15, 0,   0, 0},
};
/* hit connect: dull noise burst */
static const SfxStep stHit[] = {
    {0,  4, 500, 1, 2},
    {3,  7, 380, 1, 2},
    {4, 15, 0,   0, 0},
};
/* zombie groan: low wobble */
static const SfxStep stGroan[] = {
    {0,  9, 1100, 0, 0},
    {6,  8, 980,  0, 0},
    {6,  9, 1120, 0, 0},
    {8, 10, 900,  0, 0},
    {8, 15, 0,    0, 0},
};
/* screamer shriek: fast up-down sweep */
static const SfxStep stScream[] = {
    {0,  5, 220, 0, 0},
    {3,  4, 90,  0, 0},
    {3,  3, 60,  0, 0},
    {3,  4, 110, 0, 0},
    {3,  5, 180, 0, 0},
    {4,  8, 260, 0, 0},
    {5, 15, 0,   0, 0},
};
/* brute blow: deep thud */
static const SfxStep stBrute[] = {
    {0,  3, 1200, 1, 1},
    {4,  5, 900,  0, 0},
    {6,  8, 700,  0, 0},
    {6, 15, 0,    0, 0},
};
/* door: creak sweep down */
static const SfxStep stDoor[] = {
    {0,  8, 500, 0, 0},
    {5,  8, 420, 0, 0},
    {5,  9, 360, 0, 0},
    {5, 10, 300, 0, 0},
    {6, 15, 0,   0, 0},
};
/* use item: soft blip up */
static const SfxStep stUse[] = {
    {0,  9, 180, 0, 0},
    {5,  8, 120, 0, 0},
    {5,  8, 90,  0, 0},
    {5, 15, 0,   0, 0},
};
/* player hurt: harsh burst */
static const SfxStep stHurt[] = {
    {0,  4, 620, 1, 2},
    {2,  5, 300, 0, 0},
    {5,  7, 180, 0, 0},
    {6, 15, 0,   0, 0},
};
/* menu blip */
static const SfxStep stMenu[] = {
    {0, 10, 90, 0, 0},
    {4, 15, 0,  0, 0},
};
/* save chime */
static const SfxStep stSave[] = {
    {0,  8, 110, 0, 0},
    {5,  8, 88,  0, 0},
    {5,  7, 70,  0, 0},
    {8, 15, 0,   0, 0},
};

#define DEF(s) { s, sizeof(s) / sizeof(SfxStep) }
static const SfxDef sfxDefs[] = {
    { NULL, 0 },                    /* SFX_NONE */
    DEF(stPickup),
    DEF(stKnife),
    DEF(stHit),
    DEF(stGroan),
    DEF(stScream),
    DEF(stBrute),
    DEF(stDoor),
    DEF(stUse),                     /* SFX_STEP unused: footsteps are direct */
    DEF(stUse),
    DEF(stHurt),
    DEF(stMenu),
    DEF(stSave),
};

/* active sfx slots */
typedef struct {
    const SfxStep *steps;
    u8 len;
    u8 idx;
    u8 wait;
    u8 ch;
    bool active;
} SfxState;

static SfxState slotA, slotB;
static u16 ambientMode = AMB_NONE;
static u16 ambTimer = 0;
static u16 ambTone = 900;
static u16 windBurst = 0;

static void ch_tone(u8 ch, u16 tone, u8 vol)
{
    PSG_setTone(ch, tone);
    PSG_setEnvelope(ch, vol);
}

static void ch_noise(u8 vol, u8 mode, u8 freq)
{
    PSG_setNoise(mode == 2 ? 0 : 1, freq);   /* 0 periodic, 1 white */
    PSG_setEnvelope(CH_NOISE, vol);
}

static bool slot_start(SfxState *s, u16 id, u8 ch)
{
    const SfxDef *d = &sfxDefs[id];
    if (!d->steps) return FALSE;
    s->steps = d->steps;
    s->len = d->len;
    s->idx = 0;
    s->wait = 0;
    s->ch = ch;
    s->active = TRUE;
    /* apply first step immediately */
    s->wait = 0;
    return TRUE;
}

static void slot_update(SfxState *s)
{
    while (s->active)
    {
        if (s->wait > 0)
        {
            s->wait--;
            return;
        }
        if (s->idx >= s->len)
        {
            PSG_setEnvelope(s->ch, 15);
            s->active = FALSE;
            return;
        }
        const SfxStep *st = &s->steps[s->idx++];
        s->wait = st->wait;
        if (st->noiseMode)
            ch_noise(st->vol, st->noiseMode, st->noiseFreq);
        else
            ch_tone(s->ch, st->tone, st->vol);
    }
}

void SFX_init(void)
{
    slotA.active = FALSE;
    slotB.active = FALSE;
    ambientMode = AMB_NONE;
    PSG_reset();
}

void SFX_play(u16 id)
{
    if (id == SFX_NONE) return;
    /* route: knife/hit/brute/hurt prefer noise-capable slot A */
    if (id == SFX_KNIFE || id == SFX_HIT || id == SFX_BRUTE || id == SFX_HURT)
    {
        if (!slotA.active) { slot_start(&slotA, id, CH_A); return; }
    }
    if (id == SFX_STEP) return;    /* handled directly by player */
    if (!slotA.active) { slot_start(&slotA, id, CH_A); return; }
    if (!slotB.active) { slot_start(&slotB, id, CH_B); return; }
    /* drop new sound if both busy */
}

void SFX_footstep(void)
{
    ch_noise(9, 1, 1);
}

void SFX_setAmbient(u16 mode)
{
    if (mode == ambientMode) return;
    ambientMode = mode;
    ambTimer = 0;
    ambTone = 900;
    if (mode == AMB_NONE)
        PSG_setEnvelope(CH_AMB, 15);
    if (mode == AMB_RADIO)
        ch_noise(11, 1, 2);     /* sustained static */
}

void SFX_signalBeep(u16 tone)
{
    ch_tone(CH_B, tone, 8);
}

void SFX_signalSilence(void)
{
    PSG_setEnvelope(CH_B, 15);
}

void SFX_stopAll(void)
{
    PSG_reset();
    slotA.active = FALSE;
    slotB.active = FALSE;
    ambientMode = AMB_NONE;
}

void SFX_update(void)
{
    slot_update(&slotA);
    slot_update(&slotB);

    /* ambient */
    ambTimer++;
    if (ambientMode == AMB_WIND)
    {
        /* slow low drone with wobble + occasional gust */
        if ((ambTimer & 7) == 0)
        {
            ambTone = 900 + ((ambTimer >> 3) & 0x0F) * 24;
            ch_tone(CH_AMB, ambTone, 13);
        }
        if ((ambTimer % 512) == 256)
            windBurst = 90;
        if (windBurst > 0)
        {
            windBurst--;
            if ((windBurst & 15) == 8)
                ch_noise(13, 1, 0);
        }
    }
    else if (ambientMode == AMB_RADIO)
    {
        /* static already sustained; add crackle */
        if ((ambTimer % 24) == 0)
            ch_noise(12, 1, 3);
        if ((ambTimer % 24) == 12)
            ch_noise(11, 1, 2);
    }
}
