/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied warranty.
  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:
  1. The origin of this software must not be misrepresented.
  2. Altered source versions must be plainly marked as such.
  3. This notice may not be removed or altered from any source distribution.

  PS5 audio backend, adapted to SDL3's device callbacks from the PS5 SDL2
  backend and the PS5 Vulkan Template's measured audio contract.
*/
#include "SDL_internal.h"
#include "../SDL_sysaudio.h"
#include <stdint.h>

extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int32_t user, int32_t type, int32_t index,
                          uint32_t grain, uint32_t rate, uint32_t format);
extern int sceAudioOutOutput(int32_t handle, const void* samples);
extern int sceAudioOutClose(int32_t handle);

struct SDL_PrivateAudioData {
    int port;
    Uint8* buffer;
};

static bool PS5AUDIO_OpenDevice(SDL_AudioDevice* device)
{
    device->hidden = SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) return false;
    device->hidden->port = -1;
    device->spec.freq = 48000;
    device->spec.channels = 2;
    device->spec.format = SDL_AUDIO_F32;
    device->sample_frames = 256;
    SDL_UpdatedAudioDeviceFormat(device);
    device->hidden->buffer = SDL_aligned_alloc(64, device->buffer_size);
    if (!device->hidden->buffer) return false;
    SDL_memset(device->hidden->buffer, 0, device->buffer_size);
    // System user's main output, 256 frames, 48 kHz, float stereo (format 4).
    device->hidden->port = sceAudioOutOpen(0xff, 0, 0, 256, 48000, 4);
    if (device->hidden->port < 0)
        return SDL_SetError("PS5 sceAudioOutOpen failed: 0x%08x", (unsigned)device->hidden->port);
    return true;
}

static bool PS5AUDIO_WaitDevice(SDL_AudioDevice* device)
{
    // sceAudioOutOutput blocks until the hardware accepts the next grain.
    return true;
}

static Uint8* PS5AUDIO_GetDeviceBuf(SDL_AudioDevice* device, int* size)
{
    *size = device->buffer_size;
    return device->hidden->buffer;
}

static bool PS5AUDIO_PlayDevice(SDL_AudioDevice* device, const Uint8* buffer, int size)
{
    if (size != device->buffer_size) return SDL_SetError("PS5 audio grain size changed");
    const int result = sceAudioOutOutput(device->hidden->port, buffer);
    if (result < 0) return SDL_SetError("PS5 audio output failed: 0x%08x", (unsigned)result);
    return true;
}

static void PS5AUDIO_CloseDevice(SDL_AudioDevice* device)
{
    if (!device->hidden) return;
    if (device->hidden->port >= 0) {
        sceAudioOutOutput(device->hidden->port, NULL);
        sceAudioOutClose(device->hidden->port);
    }
    SDL_aligned_free(device->hidden->buffer);
    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool PS5AUDIO_Init(SDL_AudioDriverImpl* impl)
{
    const int result = sceAudioOutInit();
    if (result < 0 && result != (int)0x8026000e)
        return SDL_SetError("PS5 audio init failed: 0x%08x", (unsigned)result);
    impl->OpenDevice = PS5AUDIO_OpenDevice;
    impl->WaitDevice = PS5AUDIO_WaitDevice;
    impl->GetDeviceBuf = PS5AUDIO_GetDeviceBuf;
    impl->PlayDevice = PS5AUDIO_PlayDevice;
    impl->CloseDevice = PS5AUDIO_CloseDevice;
    impl->OnlyHasDefaultPlaybackDevice = true;
    return true;
}

AudioBootStrap PS5AUDIO_bootstrap = {"ps5", "PS5 native audio", PS5AUDIO_Init, false};
