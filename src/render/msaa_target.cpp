#include "render/msaa_target.hpp"

#include <rlgl.h>
#include "external/glad.h"

#include <algorithm>

namespace slopengine {

MsaaTarget::~MsaaTarget() {
    unload();
}

void MsaaTarget::unload() {
    if (colorRenderbuffer != 0) {
        glDeleteRenderbuffers(1, &colorRenderbuffer);
    }
    if (target.id != 0) {
        rlUnloadFramebuffer(target.id);
    }
    colorRenderbuffer = 0;
    target = RenderTexture2D{};
    samples = 0;
    width = 0;
    height = 0;
}

int maxMsaaSamples() {
    static int cached = -1;
    if (cached < 0) {
        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        cached = std::max(static_cast<int>(maxSamples), 0);
    }
    return cached;
}

std::vector<int> msaaSampleOptions() {
    std::vector<int> options{0};
    for (const int count : {2, 4, 8, 16}) {
        if (count <= maxMsaaSamples()) {
            options.push_back(count);
        }
    }
    return options;
}

bool ensureMsaaTarget(MsaaTarget& msaa, int width, int height, int samples) {
    samples = std::min(samples, maxMsaaSamples());
    if (samples < 2 || width <= 0 || height <= 0) {
        msaa.unload();
        return false;
    }
    if (msaa.target.id != 0 && msaa.width == width && msaa.height == height && msaa.samples == samples) {
        return true;
    }
    msaa.unload();

    const unsigned int fbo = rlLoadFramebuffer();
    if (fbo == 0) {
        return false;
    }

    GLuint renderbuffers[2] = {0, 0};
    glGenRenderbuffers(2, renderbuffers);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[0]);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[1]);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    rlFramebufferAttach(fbo, renderbuffers[0], RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_RENDERBUFFER, 0);
    rlFramebufferAttach(fbo, renderbuffers[1], RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);

    if (!rlFramebufferComplete(fbo)) {
        TraceLog(LOG_WARNING, "MSAA: %dx framebuffer is incomplete", samples);
        glDeleteRenderbuffers(1, &renderbuffers[0]);
        rlUnloadFramebuffer(fbo);
        return false;
    }

    msaa.target.id = fbo;
    msaa.target.texture.width = width;
    msaa.target.texture.height = height;
    msaa.colorRenderbuffer = renderbuffers[0];
    msaa.samples = samples;
    msaa.width = width;
    msaa.height = height;
    return true;
}

void resolveMsaaTarget(const MsaaTarget& msaa, const RenderTexture2D& destination) {
    if (msaa.target.id == 0 || destination.id == 0) {
        return;
    }
    rlBindFramebuffer(RL_READ_FRAMEBUFFER, msaa.target.id);
    rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, destination.id);
    rlBlitFramebuffer(0, 0, msaa.width, msaa.height, 0, 0, msaa.width, msaa.height, GL_COLOR_BUFFER_BIT);
    rlDisableFramebuffer();
}

}
