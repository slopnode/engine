#pragma once

#include "core/frame_perf.hpp"

namespace slopengine {

void gpuTimerFrameBegin(FramePerfStats* stats);
void gpuTimerMark(GpuPass pass);

}
