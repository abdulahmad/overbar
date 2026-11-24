/* SPDX-License-Identifier: CC0-1.0
 * The menu's navigation sounds, made in code (see include/overbar/sfx.h for their licence). The "soft" set: move = a short rounded tick,
 * select = a rising two-note chime, back = a falling two-note one. */
#include <math.h>
#include <string.h>
#include "overbar/sfx.h"
#ifdef OB_SDL_BASE
#include <SDL.h>
#endif

#define SR 44100
#define MAXN 12000

static short buf[OB_SFX_COUNT][MAXN];
static int len[OB_SFX_COUNT];
static int made;

/* a sine of freq Hz lasting dur s, under an envelope: a 2 ms attack, a 40 ms release and an exponential fall of the given curve */
static int tone(float *out, float freq, float dur, float curve)
{
    int n = (int)(SR * dur);
    double ph = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / SR, total = (double)n / SR;
        double a = t / 0.002 < 1 ? t / 0.002 : 1, r = (total - t) / 0.04;
        if (r > 1) r = 1;
        if (r < 0) r = 0;
        out[i] = (float)(sin(ph) * a * r * exp(-curve * t / total));
        ph += 2 * 3.14159265358979 * freq / SR;
    }
    return n;
}

static void make(int kind, float f1, float d1, float c1, float f2, float d2, float c2, float gain)
{
    static float tmp[MAXN];
    int n = tone(tmp, f1, d1, c1), peak_i;
    float peak = 0;
    if (f2 > 0) n += tone(tmp + n, f2, d2, c2);
    for (peak_i = 0; peak_i < n; peak_i++) if (fabsf(tmp[peak_i]) > peak) peak = fabsf(tmp[peak_i]);
    if (peak < 1e-9f) peak = 1;
    for (int i = 0; i < n; i++) buf[kind][i] = (short)(tmp[i] / peak * gain * 32767);
    len[kind] = n;
}

static void build(void)
{
    if (made) return;
    make(OB_SFX_MOVE, 880, 0.05f, 6, 0, 0, 0, 0.72f);
    make(OB_SFX_SELECT, 660, 0.07f, 4, 990, 0.14f, 4, 0.8f);
    make(OB_SFX_BACK, 740, 0.07f, 4, 494, 0.14f, 4, 0.8f);
    made = 1;
}

const short *ob_sfx_samples(int kind, int *n)
{
    build();
    if (kind < 0 || kind >= OB_SFX_COUNT) { *n = 0; return NULL; }
    *n = len[kind];
    return buf[kind];
}

#ifdef OB_SDL_BASE
static SDL_AudioDeviceID dev;
static int tried;

void ob_sfx_play(int kind, int volume)
{
    short out[MAXN];
    int n;
    const short *s;
    if (volume <= 0 || kind < 0 || kind >= OB_SFX_COUNT) return;
    if (!dev && !tried) {
        SDL_AudioSpec want;
        tried = 1;
        if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
        SDL_zero(want);
        want.freq = SR; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (dev) SDL_PauseAudioDevice(dev, 0);
    }
    if (!dev) return;
    s = ob_sfx_samples(kind, &n);
    if (volume > 100) volume = 100;
    for (int i = 0; i < n; i++) out[i] = (short)(s[i] * volume / 100 / 2);   /* half of full scale at volume 100: a UI sound sits under the game */
    SDL_ClearQueuedAudio(dev);
    SDL_QueueAudio(dev, out, (Uint32)n * 2);
}

void ob_sfx_close(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
    tried = 0;
}
#else
void ob_sfx_play(int kind, int volume) { (void)kind; (void)volume; }
void ob_sfx_close(void) {}
#endif
