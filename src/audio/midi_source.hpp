#pragma once

#include <soloud.h>

#include <array>
#include <vector>

struct tsf;
struct tml_message;

namespace slopengine {

class MidiSource;

class MidiSourceInstance : public SoLoud::AudioSourceInstance {
public:
    explicit MidiSourceInstance(MidiSource& parent);
    ~MidiSourceInstance() override;

    unsigned int getAudio(float* aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize) override;
    bool hasEnded() override;

private:
    tsf* synth_ = nullptr;
    const tml_message* firstMessage_ = nullptr;
    const tml_message* nextMessage_ = nullptr;
    double msec_ = 0.0;
    std::vector<float> scratch_;
    std::array<int, 16> channelProgramOverrides_{};
};

class MidiSource : public SoLoud::AudioSource {
public:
    // channelProgramOverrides[channel] forces that MIDI channel's GM program regardless of
    // Program Change events in the file; -1 means "no override, use the file's own program
    // changes" (the caller is responsible for filling unused channels with -1).
    MidiSource(
        tsf* baseSynth,
        const tml_message* messages,
        float sampleRate,
        std::array<int, 16> channelProgramOverrides);

    SoLoud::AudioSourceInstance* createInstance() override;

    tsf* baseSynth() const { return baseSynth_; }
    const tml_message* messages() const { return messages_; }
    const std::array<int, 16>& channelProgramOverrides() const { return channelProgramOverrides_; }

private:
    tsf* baseSynth_;
    const tml_message* messages_;
    std::array<int, 16> channelProgramOverrides_;
};

}
