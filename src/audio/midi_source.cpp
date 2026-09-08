#include "audio/midi_source.hpp"

#include "tsf.h"
#include "tml.h"

namespace slopengine {

namespace {
constexpr unsigned int kMidiEventBlockSamples = 64;
}

MidiSourceInstance::MidiSourceInstance(MidiSource& parent)
    : synth_(tsf_copy(parent.baseSynth())),
      firstMessage_(parent.messages()),
      nextMessage_(parent.messages()),
      channelProgramOverrides_(parent.channelProgramOverrides()) {
    // Applied up front so an overridden channel plays its forced instrument from the very first
    // note, even if the file never sends a Program Change for it (silently defaulting to 0).
    for (std::size_t channel = 0; channel < channelProgramOverrides_.size(); ++channel) {
        const int program = channelProgramOverrides_[channel];
        if (program >= 0) {
            tsf_channel_set_presetnumber(synth_, static_cast<int>(channel), program, channel == 9);
        }
    }
}

MidiSourceInstance::~MidiSourceInstance() {
    if (synth_ != nullptr) {
        tsf_close(synth_);
    }
}

unsigned int MidiSourceInstance::getAudio(float* aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize) {
    if (synth_ == nullptr) {
        return 0;
    }

    unsigned int written = 0;
    unsigned int remaining = aSamplesToRead;
    while (remaining > 0) {
        const unsigned int block = remaining > kMidiEventBlockSamples ? kMidiEventBlockSamples : remaining;

        msec_ += block * (1000.0 / static_cast<double>(mBaseSamplerate));
        for (; nextMessage_ != nullptr && msec_ >= nextMessage_->time; nextMessage_ = nextMessage_->next) {
            switch (nextMessage_->type) {
                case TML_PROGRAM_CHANGE:
                    if (channelProgramOverrides_[nextMessage_->channel] < 0) {
                        tsf_channel_set_presetnumber(
                            synth_, nextMessage_->channel, nextMessage_->program, nextMessage_->channel == 9);
                    }
                    break;
                case TML_NOTE_ON:
                    tsf_channel_note_on(
                        synth_, nextMessage_->channel, nextMessage_->key, nextMessage_->velocity / 127.0f);
                    break;
                case TML_NOTE_OFF:
                    tsf_channel_note_off(synth_, nextMessage_->channel, nextMessage_->key);
                    break;
                case TML_PITCH_BEND:
                    tsf_channel_set_pitchwheel(synth_, nextMessage_->channel, nextMessage_->pitch_bend);
                    break;
                case TML_CONTROL_CHANGE:
                    tsf_channel_midi_control(
                        synth_, nextMessage_->channel, nextMessage_->control, nextMessage_->control_value);
                    break;
                default:
                    break;
            }
        }

        if (nextMessage_ == nullptr && firstMessage_ != nullptr && (mFlags & AudioSourceInstance::LOOPING)) {
            tsf_note_off_all(synth_);
            nextMessage_ = firstMessage_;
            msec_ = 0.0;
            ++mLoopCount;
        }

        if (scratch_.size() < static_cast<std::size_t>(block) * 2) {
            scratch_.resize(static_cast<std::size_t>(block) * 2);
        }
        tsf_render_float(synth_, scratch_.data(), static_cast<int>(block), 0);
        for (unsigned int i = 0; i < block; ++i) {
            aBuffer[written + i] = scratch_[i * 2];
            aBuffer[aBufferSize + written + i] = scratch_[i * 2 + 1];
        }

        written += block;
        remaining -= block;
    }
    return written;
}

bool MidiSourceInstance::hasEnded() {
    if (synth_ == nullptr) {
        return true;
    }
    if (mFlags & AudioSourceInstance::LOOPING) {
        return false;
    }
    return nextMessage_ == nullptr && tsf_active_voice_count(synth_) == 0;
}

MidiSource::MidiSource(
    tsf* baseSynth, const tml_message* messages, float sampleRate, std::array<int, 16> channelProgramOverrides)
    : baseSynth_(baseSynth), messages_(messages), channelProgramOverrides_(channelProgramOverrides) {
    mChannels = 2;
    mBaseSamplerate = sampleRate;
}

SoLoud::AudioSourceInstance* MidiSource::createInstance() {
    return new MidiSourceInstance(*this);
}

}
