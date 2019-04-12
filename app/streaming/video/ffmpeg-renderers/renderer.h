#pragma once

#include <SDL.h>

#include "streaming/video/decoder.h"
#include "streaming/video/overlaymanager.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

class IFFmpegRenderer : public Overlay::IOverlayRenderer {
public:
    enum FramePacingConstraint {
        PACING_FORCE_OFF,
        PACING_FORCE_ON,
        PACING_ANY
    };

    virtual bool initialize(PDECODER_PARAMETERS params) = 0;
    virtual bool prepareDecoderContext(AVCodecContext* context) = 0;
    virtual void renderFrame(AVFrame* frame) = 0;
    virtual bool needsTestFrame() = 0;
    virtual int getDecoderCapabilities() = 0;
    virtual FramePacingConstraint getFramePacingConstraint() = 0;
    virtual bool isRenderThreadSupported() = 0;

    // IOverlayRenderer
    virtual void notifyOverlayUpdated(Overlay::OverlayType) override {
        // Nothing
    }
};
