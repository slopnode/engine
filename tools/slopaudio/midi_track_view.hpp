#pragma once

#include "asset_info.hpp"

#include "imgui.h"

namespace slopaudio {

/** Stable color for a MIDI channel (0-15), spread by golden-angle hue stepping so any subset of
 *  channels stays visually distinct. Channel 9 (the GM percussion channel) always gets a fixed
 *  neutral color instead of a hue, since it isn't a pitched instrument. */
ImU32 midiChannelColor(int channel);

/** Draws the color-swatch legend mapping each channel present in `midi` to its GM instrument
 *  name (or "Drums" for channel 9), wrapping across as many rows as `width` requires. Meant for
 *  the Inspector panel, so the color-to-instrument mapping used by drawMidiTrackPanel's roll and
 *  by drawMidiChannelLegend itself stays readable next to whatever else is shown about the
 *  asset. */
void drawMidiChannelLegend(const MidiTrackView& midi, float width);

/**
 * Draws a zoomable, horizontally scrollable piano-roll view of a parsed MIDI file: one colored
 * rectangle per note, colored by `midiChannelColor(channel)`, against a time ruler and a pitch
 * gutter. `windowMs` behaves like drawWaveformPanel's: mutable zoom span in milliseconds,
 * adjustable via +/- buttons, the ms input box, or mouse wheel over the roll; this function never
 * resets it on its own. `playheadFraction` draws a progress line like drawWaveformPanel's when in
 * [0, 1]; pass a negative value to omit it (e.g. when the asset can't be previewed).
 */
void drawMidiTrackPanel(const MidiTrackView& midi, float& windowMs, float playheadFraction, ImVec2 size);

}
