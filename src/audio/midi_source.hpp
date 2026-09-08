#pragma once

#include <soloud.h>

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
};

class MidiSource : public SoLoud::AudioSource {
public:
    MidiSource(tsf* baseSynth, const tml_message* messages, float sampleRate);

    SoLoud::AudioSourceInstance* createInstance() override;

    tsf* baseSynth() const { return baseSynth_; }
    const tml_message* messages() const { return messages_; }

private:
    tsf* baseSynth_;
    const tml_message* messages_;
};

}
