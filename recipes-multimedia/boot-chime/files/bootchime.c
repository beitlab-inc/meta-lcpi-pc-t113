/*
 * bootchime - a short, original "power-on" chime for the LCPI-PC-T113.
 *
 * Synthesises a warm, bright major chord (A major spread across several
 * octaves) with a soft attack and a long exponential decay, then plays it once
 * through the default ALSA device. It is deliberately NOT a copy of any
 * vendor's start-up sound; it is generated from scratch here.
 *
 * Design notes:
 *   - 44100 Hz, 16-bit signed, stereo.
 *   - Each note carries a touch of 2nd harmonic for shimmer; the right channel
 *     is detuned by a few cents to give a gentle stereo "chorus" width.
 *   - A 15 ms attack and a 150 ms release taper avoid clicks at both ends.
 *   - If the audio device can't be opened we exit 0 so the boot is never held
 *     up or marked failed because of the chime.
 *
 * SPDX-License-Identifier: MIT
 */

#include <alsa/asoundlib.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RATE        44100u
#define CHANNELS    2u
#define DURATION_S  2.0
#define MASTER_GAIN 0.55        /* headroom below full scale               */

#define ATTACK_S    0.015
#define RELEASE_S   0.150
#define DECAY_TAU_S 0.95        /* exponential decay time constant         */

/* A-major chord voiced across octaves: A2 E3 A3 C#4 E4 A4 C#5.
 * Slightly higher weight on the low/mid notes keeps it warm, not shrill. */
static const double kFreqs[]   = { 110.00, 164.81, 220.00, 277.18,
                                   329.63, 440.00, 554.37 };
static const double kWeights[] = {  1.00,   0.85,   0.90,   0.75,
                                    0.70,   0.55,   0.40 };
#define N_NOTES ((int)(sizeof(kFreqs) / sizeof(kFreqs[0])))

static double envelope(double t, double total)
{
    double a = (t < ATTACK_S) ? (t / ATTACK_S) : 1.0;
    double d = exp(-t / DECAY_TAU_S);
    double r = 1.0;
    double from_end = total - t;
    if (from_end < RELEASE_S)
        r = from_end / RELEASE_S;
    if (r < 0.0)
        r = 0.0;
    return a * d * r;
}

/* One channel's chord value at time t; detune shifts pitch for stereo width. */
static double voice(double t, double detune)
{
    double sum = 0.0, wsum = 0.0;
    int i;
    for (i = 0; i < N_NOTES; i++) {
        double f = kFreqs[i] * detune;
        double w = kWeights[i];
        double base = sin(2.0 * M_PI * f * t);
        double harm = 0.35 * sin(2.0 * M_PI * 2.0 * f * t);
        sum  += w * (base + harm);
        wsum += w * 1.35;               /* max per-note contribution */
    }
    return sum / wsum;                  /* normalised to roughly [-1, 1] */
}

int main(void)
{
    snd_pcm_t *pcm = NULL;
    unsigned int rate = RATE;
    snd_pcm_uframes_t total = (snd_pcm_uframes_t)(DURATION_S * RATE);
    int16_t *buf = NULL;
    snd_pcm_uframes_t n;
    const char *dev = getenv("BOOTCHIME_PCM");

    if (!dev)
        dev = "default";

    if (snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0)
        return 0;                       /* no audio: don't hold up boot */

    if (snd_pcm_set_params(pcm,
                           SND_PCM_FORMAT_S16_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED,
                           CHANNELS,
                           rate,
                           1,           /* allow ALSA resampling */
                           500000) < 0) /* 0.5s max latency */
    {
        snd_pcm_close(pcm);
        return 0;
    }

    buf = malloc((size_t)total * CHANNELS * sizeof(int16_t));
    if (!buf) {
        snd_pcm_close(pcm);
        return 0;
    }

    for (n = 0; n < total; n++) {
        double t   = (double)n / (double)RATE;
        double env = envelope(t, DURATION_S) * MASTER_GAIN;

        double l = voice(t, 1.0000) * env;
        double r = voice(t, 1.0018) * env;   /* ~3 cents up: subtle width */

        l = (l >  1.0) ?  1.0 : (l < -1.0 ? -1.0 : l);
        r = (r >  1.0) ?  1.0 : (r < -1.0 ? -1.0 : r);

        buf[n * CHANNELS + 0] = (int16_t)(l * 32767.0);
        buf[n * CHANNELS + 1] = (int16_t)(r * 32767.0);
    }

    n = 0;
    while (n < total) {
        snd_pcm_sframes_t wrote = snd_pcm_writei(pcm, buf + n * CHANNELS,
                                                 total - n);
        if (wrote < 0) {
            wrote = snd_pcm_recover(pcm, (int)wrote, 1);
            if (wrote < 0)
                break;
            continue;
        }
        n += (snd_pcm_uframes_t)wrote;
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    free(buf);
    return 0;
}
