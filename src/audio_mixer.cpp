#include "audio_mixer.h"
#include "common.h"
#include "patch.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace m1 {

namespace {

constexpr int kMaxSndSlots = 272; // MPSSNDR.DLL supports slots 0..0x10F (271)

enum AudioChannel : uint8_t {
    AUDIO_SFX    = 0,
    AUDIO_MUSIC  = 1,
    AUDIO_ENGINE = 2,
    AUDIO_VOICE  = 3,
};

AudioChannel g_slotCat[kMaxSndSlots]     = {};
int32_t      g_slotBaseVol[kMaxSndSlots] = {};
bool         g_slotSeen[kMaxSndSlots]    = {};

using MpsLoadFn   = int(__cdecl*)(int slot, const char* path, void* params);
using MpsPlayFn   = int(__cdecl*)(int slot, void* params);
using MpsSetVolFn = int(__cdecl*)(int slot, int32_t vol);

static std::string ToLowerStr(const char* s) {
    std::string out;
    if (!s) return out;
    out.reserve(std::strlen(s));
    while (*s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*s++))));
    }
    return out;
}

static AudioChannel ClassifySlot(int slot, const char* path, uintptr_t callerRa) {
    // 0x456F03 is M1TP2's crew radio / voice speech loader
    if (callerRa >= 0x456E00u && callerRa < 0x457200u) {
        return AUDIO_VOICE;
    }
    if (slot == 1) {
        return AUDIO_MUSIC;
    }
    std::string low = ToLowerStr(path);
    if (low.find("mus") != std::string::npos ||
        low.find("wrapper") != std::string::npos ||
        low.find("briefing") != std::string::npos ||
        low.find("tankdead") != std::string::npos) {
        return AUDIO_MUSIC;
    }
    if (low.find("speech") != std::string::npos ||
        low.find("voice") != std::string::npos ||
        low.find("radio") != std::string::npos ||
        low.find("vox") != std::string::npos) {
        return AUDIO_VOICE;
    }
    if (low.find("idle") != std::string::npos ||
        low.find("tred") != std::string::npos ||
        low.find("turret") != std::string::npos ||
        low.find("loop") != std::string::npos ||
        low.find("choper") != std::string::npos ||
        low.find("diesel") != std::string::npos ||
        low.find("turbine") != std::string::npos) {
        return AUDIO_ENGINE;
    }
    return AUDIO_SFX;
}

static float GetChannelGain(AudioChannel ch) {
    if (g_cfg.audioMute) return 0.0f;
    float master = std::clamp(g_cfg.audioMasterVol, 0.0f, 1.0f);
    float sub = 1.0f;
    switch (ch) {
        case AUDIO_MUSIC:  sub = std::clamp(g_cfg.audioMusicVol,  0.0f, 1.0f); break;
        case AUDIO_ENGINE: sub = std::clamp(g_cfg.audioEngineVol, 0.0f, 1.0f); break;
        case AUDIO_VOICE:  sub = std::clamp(g_cfg.audioVoiceVol,  0.0f, 1.0f); break;
        case AUDIO_SFX:
        default:           sub = std::clamp(g_cfg.audioSfxVol,    0.0f, 1.0f); break;
    }
    return master * sub;
}

// MPSSNDR!SetVol maps vol [0..400] linearly to DirectSound centibels: dB*100 = vol*10 - 4000
// (so 10 MPSSNDR units = 1 dB). Apply exact logarithmic 20*log10(gain) dB attenuation!
static int32_t ScaleMpsVolume(int slot, int32_t baseVol) {
    int32_t clampedBase = std::clamp(baseVol, 0, 400);
    AudioChannel ch = (slot >= 0 && slot < kMaxSndSlots) ? g_slotCat[slot] : AUDIO_SFX;
    float gain = GetChannelGain(ch);
    if (gain <= 0.005f) return 0;
    if (gain >= 0.995f) return clampedBase;
    double dB = 20.0 * std::log10(static_cast<double>(gain));
    int32_t out = static_cast<int32_t>(std::lround(static_cast<double>(clampedBase) + dB * 10.0));
    return std::clamp(out, 0, 400);
}

int __cdecl Hook_SndLoad(int slot, const char* path, void* params) {
    uint32_t state = mem<uint32_t>(addr::g_SndInitState);
    if (state == 0) return 4;

    if (slot >= 0 && slot < kMaxSndSlots) {
        uintptr_t ra = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        g_slotCat[slot]     = ClassifySlot(slot, path, ra);
        g_slotBaseVol[slot] = 400;
        g_slotSeen[slot]    = true;
    }

    auto fn = reinterpret_cast<MpsLoadFn>(mem<uintptr_t>(addr::g_SndFnLoad));
    if (!fn) return 4;

    // If LoadSnd is given an initial params block (e.g. menu music at 0x4273CC with vol=360),
    // scale its volume on a local copy so playback starts at the exact mixed level.
    if (params && slot >= 0 && slot < kMaxSndSlots) {
        int32_t copy[8];
        std::memcpy(copy, params, sizeof(copy));
        if (copy[0] >= 0 && copy[0] <= 400) {
            g_slotBaseVol[slot] = copy[0];
            copy[0] = ScaleMpsVolume(slot, g_slotBaseVol[slot]);
            return fn(slot, path, copy);
        }
    }
    return fn(slot, path, params);
}

int __cdecl Hook_SndPlay(int slot, void* params) {
    uint32_t state = mem<uint32_t>(addr::g_SndInitState);
    if (state == 0 || state == 2) return 4;

    auto fnPlay   = reinterpret_cast<MpsPlayFn>(mem<uintptr_t>(addr::g_SndFnPlay));
    auto fnSetVol = reinterpret_cast<MpsSetVolFn>(mem<uintptr_t>(addr::g_SndFnSetVol));
    if (!fnPlay) return 4;

    AudioChannel ch = (slot >= 0 && slot < kMaxSndSlots) ? g_slotCat[slot] : AUDIO_SFX;
    float gain = GetChannelGain(ch);

    if (params) {
        int32_t copy[8];
        std::memcpy(copy, params, sizeof(copy));
        int32_t baseVol = std::clamp(copy[0], 0, 400);
        if (slot >= 0 && slot < kMaxSndSlots) {
            g_slotBaseVol[slot] = baseVol;
            g_slotSeen[slot]    = true;
        }
        // For non-looped one-shot SFX/voice, skip triggering completely when muted
        if (gain <= 0.005f && ch != AUDIO_MUSIC && ch != AUDIO_ENGINE) {
            return 0;
        }
        copy[0] = ScaleMpsVolume(slot, baseVol);
        return fnPlay(slot, copy);
    }

    // Crew radio/speech calls PlaySnd(slot, nullptr) with default full volume (400)
    if (slot >= 0 && slot < kMaxSndSlots) {
        g_slotBaseVol[slot] = 400;
        g_slotSeen[slot]    = true;
    }
    if (gain <= 0.005f) {
        return 0;
    }
    int res = fnPlay(slot, nullptr);
    if (res == 0 && fnSetVol && gain < 0.995f) {
        fnSetVol(slot, ScaleMpsVolume(slot, 400));
    }
    return res;
}

int __cdecl Hook_SndSetVol(int slot, int32_t vol) {
    uint32_t state = mem<uint32_t>(addr::g_SndInitState);
    if (state == 0 || state == 2) return 4;

    if (slot >= 0 && slot < kMaxSndSlots) {
        g_slotBaseVol[slot] = std::clamp(vol, 0, 400);
        g_slotSeen[slot]    = true;
    }

    auto fn = reinterpret_cast<MpsSetVolFn>(mem<uintptr_t>(addr::g_SndFnSetVol));
    if (!fn) return 4;
    return fn(slot, ScaleMpsVolume(slot, vol));
}

} // namespace

void AudioApplySettings() {
    uint32_t state = mem<uint32_t>(addr::g_SndInitState);
    if (state == 0 || state == 2) return;
    auto fn = reinterpret_cast<MpsSetVolFn>(mem<uintptr_t>(addr::g_SndFnSetVol));
    if (!fn) return;

    for (int slot = 0; slot < kMaxSndSlots; ++slot) {
        if (!g_slotSeen[slot]) continue;
        fn(slot, ScaleMpsVolume(slot, g_slotBaseVol[slot]));
    }
}

void AudioMixerInstallHooks() {
    for (int i = 0; i < kMaxSndSlots; ++i) {
        g_slotCat[i]     = (i == 1) ? AUDIO_MUSIC : AUDIO_SFX;
        g_slotBaseVol[i] = 400;
        g_slotSeen[i]    = false;
    }
    InstallJump(addr::SndLoad,   reinterpret_cast<void*>(&Hook_SndLoad));
    InstallJump(addr::SndPlay,   reinterpret_cast<void*>(&Hook_SndPlay));
    InstallJump(addr::SndSetVol, reinterpret_cast<void*>(&Hook_SndSetVol));
    LogPrintf("[AUDIO] MPSSNDR 4-channel logarithmic mixer installed (mute=%d master=%.0f%% mus=%.0f%% sfx=%.0f%% eng=%.0f%% vox=%.0f%%)\n",
              g_cfg.audioMute ? 1 : 0,
              g_cfg.audioMasterVol * 100.0f, g_cfg.audioMusicVol * 100.0f,
              g_cfg.audioSfxVol * 100.0f, g_cfg.audioEngineVol * 100.0f,
              g_cfg.audioVoiceVol * 100.0f);
}

} // namespace m1
