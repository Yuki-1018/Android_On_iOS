/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
*/
/* AndroidEmu: goldfish/audio.c output-only port. Two bounded DMA buffers,
 * 44.1 kHz stereo S16LE matching the stock Goldfish HAL. No microphone.
 */
#include "qemu/osdep.h"
#include "android51.h"
#include "android51_host.h"
#include "audio/audio.h"
#include "system/reset.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

typedef struct GFAudio {
    Android51State *board;
    MemoryRegion io;
    QEMUSoundCard card;
    SWVoiceOut *voice;
    uint32_t address[2], length[2], offset[2], enabled, status;
    uint8_t pcm[2][65536];
    unsigned next;
} GFAudio;
static void audio_irq(GFAudio *s)
{
    qemu_set_irq(s->board->irqs[16], (s->status & s->enabled) != 0);
}
static void audio_callback(void *opaque, int free_bytes)
{
    GFAudio *s = opaque;
    for (unsigned attempt = 0; attempt < 2 && free_bytes > 0; ++attempt) {
        unsigned i = s->next;
        unsigned remaining = s->length[i] - s->offset[i];
        if (remaining) {
            size_t written = AUD_write(s->voice, s->pcm[i] + s->offset[i], MIN(remaining, free_bytes));
            android51_host_pcm(s->pcm[i] + s->offset[i], written);
            s->offset[i] += written;
            free_bytes -= written;
            if (s->offset[i] != s->length[i]) { break; }
            s->status |= 1U << i;
            s->offset[i] = s->length[i] = 0;
        }
        s->next ^= 1;
    }
    audio_irq(s);
}
static uint64_t audio_read(void *opaque, hwaddr offset, unsigned size)
{
    GFAudio *s = opaque;
    if (offset == 0) {
        uint32_t status = s->status & s->enabled;
        s->status &= ~status; audio_irq(s); return status;
    }
    return 0; /* READ_SUPPORTED=0; microphone absent. */
}
static void audio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    GFAudio *s = opaque;
    switch (offset) {
    case 4:
        s->enabled = value & 3;
        for (unsigned i = 0; i < 2; ++i) { if (!s->length[i]) { s->status |= 1U << i; } }
        AUD_set_active_out(s->voice, s->enabled != 0);
        break;
    case 8: s->address[0] = value; break;
    case 12: s->address[1] = value; break;
    case 16: case 20: {
        unsigned i = (offset - 16) / 4;
        if (!value || value % 4 || value > sizeof(s->pcm[i]) || s->length[i]) { break; }
        if (!gf_guest_virtual(s->board, s->address[i], s->pcm[i], value, false)) { break; }
        s->length[i] = value; s->offset[i] = 0; s->status &= ~(1U << i);
        break;
    }
    }
    audio_irq(s);
}
static const MemoryRegionOps audio_ops = {
    .read = audio_read, .write = audio_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
};
static void audio_reset(void *opaque)
{
    GFAudio *s = opaque;
    memset(s->address, 0, sizeof(s->address));
    memset(s->length, 0, sizeof(s->length));
    memset(s->offset, 0, sizeof(s->offset));
    s->next = s->enabled = 0; s->status = 3;
    AUD_set_active_out(s->voice, false); audio_irq(s);
}
void gf_audio_init(Android51State *board)
{
    GFAudio *s = g_new0(GFAudio, 1);
    struct audsettings settings = {.freq = 44100, .nchannels = 2, .fmt = AUDIO_FORMAT_S16, .endianness = 0};
    s->board = board;
    if (MACHINE(board)->audiodev) {
        s->card.state = audio_state_by_name(MACHINE(board)->audiodev, &error_fatal);
    }
    AUD_register_card("android51.audio", &s->card, &error_fatal);
    s->voice = AUD_open_out(&s->card, NULL, "goldfish-output", s, audio_callback, &settings);
    if (!s->voice) { error_report("android51: cannot open Goldfish PCM output"); exit(EXIT_FAILURE); }
    gf_map(board, &s->io, "android51.audio", 0xff004000, &audio_ops, s);
    gf_register(board, "goldfish_audio", -1, 0xff004000, 0x1000, 16, 1);
    qemu_register_reset(audio_reset, s);
}
