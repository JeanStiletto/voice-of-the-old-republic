#include "audio_wav.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace acc::audio {

namespace {

void PutU16(unsigned char* p, uint16_t v) {
    p[0] = static_cast<unsigned char>(v & 0xff);
    p[1] = static_cast<unsigned char>(v >> 8);
}

void PutU32(unsigned char* p, uint32_t v) {
    p[0] = static_cast<unsigned char>(v & 0xff);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xff);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xff);
    p[3] = static_cast<unsigned char>(v >> 24);
}

constexpr size_t kHeaderBytes = 44;  // RIFF + fmt (16-byte PCM body) + data

}  // namespace

bool BuildMonoPcm16Wav(const float* samples, size_t frames, size_t channels,
                       size_t sampleRate, std::vector<unsigned char>& out) {
    out.clear();
    if (!samples || frames == 0 || channels == 0 || sampleRate == 0) return false;

    const uint32_t dataBytes = static_cast<uint32_t>(frames * 2);
    out.resize(kHeaderBytes + dataBytes);
    unsigned char* p = out.data();

    std::memcpy(p, "RIFF", 4);
    PutU32(p + 4, static_cast<uint32_t>(out.size() - 8));
    std::memcpy(p + 8, "WAVE", 4);
    std::memcpy(p + 12, "fmt ", 4);
    PutU32(p + 16, 16);                                          // fmt body size
    PutU16(p + 20, 1);                                           // WAVE_FORMAT_PCM
    PutU16(p + 22, 1);                                           // mono
    PutU32(p + 24, static_cast<uint32_t>(sampleRate));
    PutU32(p + 28, static_cast<uint32_t>(sampleRate * 2));       // byte rate
    PutU16(p + 32, 2);                                           // block align
    PutU16(p + 34, 16);                                          // bits per sample
    std::memcpy(p + 36, "data", 4);
    PutU32(p + 40, dataBytes);

    unsigned char* dst = p + kHeaderBytes;
    for (size_t f = 0; f < frames; ++f) {
        float sum = 0.0f;
        for (size_t c = 0; c < channels; ++c) sum += samples[f * channels + c];
        float v = sum / static_cast<float>(channels);
        if (v > 1.0f)  v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        PutU16(dst + f * 2, static_cast<uint16_t>(static_cast<int16_t>(v * 32767.0f)));
    }
    return true;
}

Levels MeasureLevels(const std::vector<float>& samples) {
    float  peak = 0.0f;
    double sumSq = 0.0;
    for (float s : samples) {
        const float a = std::fabs(s);
        if (a > peak) peak = a;
        sumSq += static_cast<double>(s) * s;
    }
    const double rms = samples.empty() ? 0.0 : std::sqrt(sumSq / samples.size());
    return Levels{20.0f * std::log10(peak),
                  static_cast<float>(20.0 * std::log10(rms))};
}

float NormalizePeak(std::vector<float>& samples, float targetPeakDb) {
    float peak = 0.0f;
    for (float s : samples) peak = std::fmax(peak, std::fabs(s));
    if (peak <= 0.0f) return 1.0f;
    const float gain = std::pow(10.0f, targetPeakDb / 20.0f) / peak;
    for (float& s : samples) s *= gain;
    return gain;
}

}  // namespace acc::audio
