#pragma once

#include "asset_info.hpp"

#include "imgui.h"

namespace slopaudio {

/** Stable color for the track at position `index` within a MidiTrackView::tracks list, spread by
 *  golden-angle hue stepping so any number of visible tracks stays maximally distinct -- two
 *  tracks that happen to share a MIDI channel (common: separate left-hand/right-hand piano
 *  tracks both on channel 0) still get different colors, since coloring is by track slot, not
 *  channel. `isPercussion` (GM channel 9) always gets a fixed neutral color instead of a hue,
 *  since it isn't a pitched instrument. */
ImU32 midiTrackColor(int index, bool isPercussion);

/** Draws the color-swatch legend mapping each track in `midi` to its name (or "Track N" if the
 *  file didn't set one) and GM instrument (or "Drums" for channel 9), wrapping across as many
 *  rows as `width` requires. Meant for the Inspector panel, so the color-to-track mapping used by
 *  drawMidiTrackPanel's roll stays readable next to whatever else is shown about the asset. */
void drawMidiChannelLegend(const MidiTrackView& midi, float width);

/**
 * Draws a zoomable, horizontally scrollable piano-roll view of a parsed MIDI file: one colored
 * rectangle per note, colored by `midiTrackColor`, against a time ruler and a pitch gutter.
 * `windowMs` behaves like drawWaveformPanel's: mutable zoom span in milliseconds, adjustable via
 * +/- buttons, the ms input box, or mouse wheel over the roll; this function never resets it on
 * its own. `playheadFraction` draws a progress line like drawWaveformPanel's when in [0, 1]; pass
 * a negative value to omit it (e.g. when the asset can't be previewed).
 */
void drawMidiTrackPanel(const MidiTrackView& midi, float& windowMs, float playheadFraction, ImVec2 size);

}
