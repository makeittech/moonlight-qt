// Nasty hack to avoid conflict between AVFoundation and
// libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "vt.h"
#include "pacer/pacer.h"
#undef AVMediaType

#include <SDL_syswm.h>
#include <Limelight.h>
#include <streaming/session.h>

#include <mach/mach_time.h>
#include <mach/machine.h>
#include <sys/sysctl.h>
#import <Cocoa/Cocoa.h>
#import <VideoToolbox/VideoToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <dispatch/dispatch.h>
#import <Metal/Metal.h>

@interface VTView : NSView
- (NSView *)hitTest:(NSPoint)point;
@end

@implementation VTView

- (NSView *)hitTest:(NSPoint)point {
    Q_UNUSED(point);
    return nil;
}

@end

class VTRenderer : public IFFmpegRenderer
{
public:
    VTRenderer()
        : m_HwContext(nullptr),
          m_DisplayLayer(nullptr),
          m_FormatDesc(nullptr),
          m_StreamView(nullptr),
          m_DisplayLink(nullptr),
          m_LastAvColorSpace(AVCOL_SPC_UNSPECIFIED),
          m_ColorSpace(nullptr),
          m_VsyncMutex(nullptr),
          m_VsyncPassed(nullptr)
    {
        SDL_zero(m_OverlayTextFields);
    }

    virtual ~VTRenderer() override
    {
        if (m_DisplayLink != nullptr) {
            CVDisplayLinkStop(m_DisplayLink);
            CVDisplayLinkRelease(m_DisplayLink);
        }

        if (m_VsyncPassed != nullptr) {
            SDL_DestroyCond(m_VsyncPassed);
        }

        if (m_VsyncMutex != nullptr) {
            SDL_DestroyMutex(m_VsyncMutex);
        }

        if (m_HwContext != nullptr) {
            av_buffer_unref(&m_HwContext);
        }

        if (m_FormatDesc != nullptr) {
            CFRelease(m_FormatDesc);
        }

        if (m_ColorSpace != nullptr) {
            CGColorSpaceRelease(m_ColorSpace);
        }

        for (int i = 0; i < Overlay::OverlayMax; i++) {
            if (m_OverlayTextFields[i] != nullptr) {
                [m_OverlayTextFields[i] removeFromSuperview];
            }
        }

        if (m_StreamView != nullptr) {
            [m_StreamView removeFromSuperview];
        }
    }

    static
    CVReturn
    displayLinkOutputCallback(
        CVDisplayLinkRef displayLink,
        const CVTimeStamp* /* now */,
        const CVTimeStamp* /* vsyncTime */,
        CVOptionFlags,
        CVOptionFlags*,
        void *displayLinkContext)
    {
        auto me = reinterpret_cast<VTRenderer*>(displayLinkContext);

        SDL_assert(displayLink == me->m_DisplayLink);

        SDL_LockMutex(me->m_VsyncMutex);
        SDL_CondSignal(me->m_VsyncPassed);
        SDL_UnlockMutex(me->m_VsyncMutex);

        return kCVReturnSuccess;
    }

    bool initializeVsyncCallback(SDL_SysWMinfo* info)
    {
        NSScreen* screen = [info->info.cocoa.window screen];
        CVReturn status;
        if (screen == nullptr) {
            // Window not visible on any display, so use a
            // CVDisplayLink that can work with all active displays.
            // When we become visible, we'll recreate ourselves
            // and associate with the new screen.
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NSWindow is not visible on any display");
            status = CVDisplayLinkCreateWithActiveCGDisplays(&m_DisplayLink);
        }
        else {
            CGDirectDisplayID displayId = [[screen deviceDescription][@"NSScreenNumber"] unsignedIntValue];
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "NSWindow on display: %x",
                        displayId);
            status = CVDisplayLinkCreateWithCGDisplay(displayId, &m_DisplayLink);
        }
        if (status != kCVReturnSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create CVDisplayLink: %d",
                         status);
            return false;
        }

        status = CVDisplayLinkSetOutputCallback(m_DisplayLink, displayLinkOutputCallback, this);
        if (status != kCVReturnSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "CVDisplayLinkSetOutputCallback() failed: %d",
                         status);
            return false;
        }

        // The CVDisplayLink callback uses these, so we must initialize them before
        // starting the callbacks.
        m_VsyncMutex = SDL_CreateMutex();
        m_VsyncPassed = SDL_CreateCond();

        status = CVDisplayLinkStart(m_DisplayLink);
        if (status != kCVReturnSuccess) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "CVDisplayLinkStart() failed: %d",
                         status);
            return false;
        }

        return true;
    }

    virtual void waitToRender() override
    {
        if (m_DisplayLink != nullptr) {
            // Vsync is enabled, so wait for a swap before returning
            SDL_LockMutex(m_VsyncMutex);
            if (SDL_CondWaitTimeout(m_VsyncPassed, m_VsyncMutex, 100) == SDL_MUTEX_TIMEDOUT) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "V-sync wait timed out after 100 ms");
            }
            SDL_UnlockMutex(m_VsyncMutex);
        }
    }

    // Caller frees frame after we return
    virtual void renderFrame(AVFrame* frame) override
    {
        OSStatus status;
        CVPixelBufferRef pixBuf = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);

        if (m_DisplayLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Resetting failed AVSampleBufferDisplay layer");

            // Trigger the main thread to recreate the decoder
            SDL_Event event;
            event.type = SDL_RENDER_TARGETS_RESET;
            SDL_PushEvent(&event);
            return;
        }

        // FFmpeg 5.0+ sets the CVPixelBuffer attachments properly now, so we don't have to
        // fix them up ourselves (except CGColorSpace and PAR attachments).

        // The VideoToolbox decoder attaches pixel aspect ratio information to the CVPixelBuffer
        // which will rescale the video stream in accordance with the host display resolution
        // to preserve the original aspect ratio of the host desktop. This behavior currently
        // differs from the behavior of all other Moonlight Qt renderers, so we will strip
        // these attachments for consistent behavior.
        CVBufferRemoveAttachment(pixBuf, kCVImageBufferPixelAspectRatioKey);

        // Reset m_ColorSpace if the colorspace changes. This can happen when
        // a game enters HDR mode (Rec 601 -> Rec 2020).
        if (frame->colorspace != m_LastAvColorSpace) {
            if (m_ColorSpace != nullptr) {
                CGColorSpaceRelease(m_ColorSpace);
                m_ColorSpace = nullptr;
            }

            switch (frame->colorspace) {
            case AVCOL_SPC_BT709:
                m_ColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceITUR_709);
                break;
            case AVCOL_SPC_BT2020_NCL:
                m_ColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceITUR_2020);
                break;
            case AVCOL_SPC_SMPTE170M:
                m_ColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
                break;
            default:
                break;
            }

            m_LastAvColorSpace = frame->colorspace;
        }

        if (m_ColorSpace != nullptr) {
            CVBufferSetAttachment(pixBuf, kCVImageBufferCGColorSpaceKey, m_ColorSpace, kCVAttachmentMode_ShouldPropagate);
        }

        // If the format has changed or doesn't exist yet, construct it with the
        // pixel buffer data
        if (!m_FormatDesc || !CMVideoFormatDescriptionMatchesImageBuffer(m_FormatDesc, pixBuf)) {
            if (m_FormatDesc != nullptr) {
                CFRelease(m_FormatDesc);
            }
            status = CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault,
                                                                  pixBuf, &m_FormatDesc);
            if (status != noErr) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "CMVideoFormatDescriptionCreateForImageBuffer() failed: %d",
                             status);
                return;
            }
        }

        // Queue this sample for the next v-sync
        CMSampleTimingInfo timingInfo = {
            .duration = kCMTimeInvalid,
            .presentationTimeStamp = CMClockMakeHostTimeFromSystemUnits(mach_absolute_time()),
            .decodeTimeStamp = kCMTimeInvalid,
        };

        CMSampleBufferRef sampleBuffer;
        status = CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault,
                                                          pixBuf,
                                                          m_FormatDesc,
                                                          &timingInfo,
                                                          &sampleBuffer);
        if (status != noErr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "CMSampleBufferCreateReadyWithImageBuffer() failed: %d",
                         status);
            return;
        }

        [m_DisplayLayer enqueueSampleBuffer:sampleBuffer];

        CFRelease(sampleBuffer);
    }

    virtual bool initialize(PDECODER_PARAMETERS params) override
    {
        int err;

        if (params->videoFormat & VIDEO_FORMAT_MASK_H264) {
            if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_H264)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "No HW accelerated H.264 decode via VT");
                return false;
            }
        }
        else if (params->videoFormat & VIDEO_FORMAT_MASK_H265) {
            if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "No HW accelerated HEVC decode via VT");
                return false;
            }

            // HEVC Main10 requires more extensive checks because there's no
            // simple API to check for Main10 hardware decoding, and if we don't
            // have it, we'll silently get software decoding with horrible performance.
            if (params->videoFormat == VIDEO_FORMAT_H265_MAIN10) {
                id<MTLDevice> device = MTLCreateSystemDefaultDevice();
                if (device == nullptr) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Unable to get default Metal device");
                    return false;
                }

                // Exclude all GPUs earlier than macOSGPUFamily2
                // https://developer.apple.com/documentation/metal/mtlfeatureset/mtlfeatureset_macos_gpufamily2_v1
                if ([device supportsFeatureSet:MTLFeatureSet_macOS_GPUFamily2_v1]) {
                    if ([device.name containsString:@"Intel"]) {
                        // 500-series Intel GPUs are Skylake and don't support Main10 hardware decoding
                        if ([device.name containsString:@" 5"]) {
                            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                        "No HEVC Main10 support on Skylake iGPU");
                            [device release];
                            return false;
                        }
                    }
                    else if ([device.name containsString:@"AMD"]) {
                        // FirePro D, M200, and M300 series GPUs don't support Main10 hardware decoding
                        if ([device.name containsString:@"FirePro D"] ||
                                [device.name containsString:@" M2"] ||
                                [device.name containsString:@" M3"]) {
                            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                        "No HEVC Main10 support on AMD GPUs until Polaris");
                            [device release];
                            return false;
                        }
                    }
                }

                [device release];
            }
        }

        SDL_SysWMinfo info;

        SDL_VERSION(&info.version);

        if (!SDL_GetWindowWMInfo(params->window, &info)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SDL_GetWindowWMInfo() failed: %s",
                        SDL_GetError());
            return false;
        }

        SDL_assert(info.subsystem == SDL_SYSWM_COCOA);

        // SDL adds its own content view to listen for events.
        // We need to add a subview for our display layer.
        NSView* contentView = info.info.cocoa.window.contentView;
        m_StreamView = [[VTView alloc] initWithFrame:contentView.bounds];

        m_DisplayLayer = [[AVSampleBufferDisplayLayer alloc] init];
        m_DisplayLayer.bounds = m_StreamView.bounds;
        m_DisplayLayer.position = CGPointMake(CGRectGetMidX(m_StreamView.bounds), CGRectGetMidY(m_StreamView.bounds));
        m_DisplayLayer.videoGravity = AVLayerVideoGravityResizeAspect;
        m_DisplayLayer.opaque = YES;

        // This workaround prevents the image from going through processing that causes some
        // color artifacts in some cases. HDR seems to be okay without this, so we'll exclude
        // it out of caution. The artifacts seem to be far more significant on M1 Macs and
        // the workaround can cause performance regressions on Intel Macs, so only use this
        // on Apple silicon.
        //
        // https://github.com/moonlight-stream/moonlight-qt/issues/493
        // https://github.com/moonlight-stream/moonlight-qt/issues/722
        if (params->videoFormat != VIDEO_FORMAT_H265_MAIN10) {
            int err;
            uint32_t cpuType;
            size_t size = sizeof(cpuType);

            // Apple Silicon Macs have CPU_ARCH_ABI64 set, so we need to mask that off.
            // For some reason, 64-bit Intel Macs don't seem to have CPU_ARCH_ABI64 set.
            err = sysctlbyname("hw.cputype", &cpuType, &size, NULL, 0);
            if (err == 0 && (cpuType & ~CPU_ARCH_MASK) == CPU_TYPE_ARM) {
                if (info.info.cocoa.window.screen != nullptr) {
                    m_DisplayLayer.shouldRasterize = YES;
                    m_DisplayLayer.rasterizationScale = info.info.cocoa.window.screen.backingScaleFactor;
                }
                else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Unable to rasterize layer due to missing NSScreen");
                    SDL_assert(false);
                }
            }
            else if (err != 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "sysctlbyname(hw.cputype) failed: %d", err);
            }
        }

        // Create a layer-hosted view by setting the layer before wantsLayer
        // This avoids us having to add our AVSampleBufferDisplayLayer as a
        // sublayer of a layer-backed view which leaves a useless layer in
        // the middle.
        m_StreamView.layer = m_DisplayLayer;
        m_StreamView.wantsLayer = YES;

        [contentView addSubview: m_StreamView];

        err = av_hwdevice_ctx_create(&m_HwContext,
                                     AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                     nullptr,
                                     nullptr,
                                     0);
        if (err < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "av_hwdevice_ctx_create() failed for VT decoder: %d",
                        err);
            return false;
        }

        if (params->enableFramePacing) {
            if (!initializeVsyncCallback(&info)) {
                return false;
            }
        }

        return true;
    }

    void updateOverlayOnMainThread(Overlay::OverlayType type)
    {
        // Lazy initialization for the overlay
        if (m_OverlayTextFields[type] == nullptr) {
            m_OverlayTextFields[type] = [[NSTextField alloc] initWithFrame:m_StreamView.bounds];
            [m_OverlayTextFields[type] setBezeled:NO];
            [m_OverlayTextFields[type] setDrawsBackground:NO];
            [m_OverlayTextFields[type] setEditable:NO];
            [m_OverlayTextFields[type] setSelectable:NO];

            switch (type) {
            case Overlay::OverlayDebug:
                [m_OverlayTextFields[type] setAlignment:NSTextAlignmentLeft];
                break;
            case Overlay::OverlayStatusUpdate:
                [m_OverlayTextFields[type] setAlignment:NSTextAlignmentRight];
                break;
            default:
                break;
            }

            SDL_Color color = Session::get()->getOverlayManager().getOverlayColor(type);
            [m_OverlayTextFields[type] setTextColor:[NSColor colorWithSRGBRed:color.r / 255.0 green:color.g / 255.0 blue:color.b / 255.0 alpha:color.a / 255.0]];
            [m_OverlayTextFields[type] setFont:[NSFont messageFontOfSize:Session::get()->getOverlayManager().getOverlayFontSize(type)]];

            [m_StreamView addSubview: m_OverlayTextFields[type]];
        }

        // Update text contents
        [m_OverlayTextFields[type] setStringValue: [NSString stringWithUTF8String:Session::get()->getOverlayManager().getOverlayText(type)]];

        // Unhide if it's enabled
        [m_OverlayTextFields[type] setHidden: !Session::get()->getOverlayManager().isOverlayEnabled(type)];
    }

    static void updateDebugOverlayOnMainThread(void* context)
    {
        VTRenderer* me = (VTRenderer*)context;

        me->updateOverlayOnMainThread(Overlay::OverlayDebug);
    }

    static void updateStatusOverlayOnMainThread(void* context)
    {
        VTRenderer* me = (VTRenderer*)context;

        me->updateOverlayOnMainThread(Overlay::OverlayStatusUpdate);
    }

    virtual void notifyOverlayUpdated(Overlay::OverlayType type) override
    {
        // We must do the actual UI updates on the main thread, so queue an
        // async callback on the main thread via GCD to do the UI update.
        switch (type) {
        case Overlay::OverlayDebug:
            dispatch_async_f(dispatch_get_main_queue(), this, updateDebugOverlayOnMainThread);
            break;
        case Overlay::OverlayStatusUpdate:
            dispatch_async_f(dispatch_get_main_queue(), this, updateStatusOverlayOnMainThread);
            break;
        default:
            break;
        }
    }

    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary**) override
    {
        context->hw_device_ctx = av_buffer_ref(m_HwContext);

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using VideoToolbox accelerated renderer");

        return true;
    }

    virtual bool needsTestFrame() override
    {
        // We used to trust VT to tell us whether decode will work, but
        // there are cases where it can lie because the hardware technically
        // can decode the format but VT is unserviceable for some other reason.
        // Decoding the test frame will tell us for sure whether it will work.
        return true;
    }

    int getDecoderColorspace() override
    {
        // macOS seems to handle Rec 601 best
        return COLORSPACE_REC_601;
    }

    int getRendererAttributes() override
    {
        // AVSampleBufferDisplayLayer supports HDR output
        return RENDERER_ATTRIBUTE_HDR_SUPPORT;
    }

private:
    AVBufferRef* m_HwContext;
    AVSampleBufferDisplayLayer* m_DisplayLayer;
    CMVideoFormatDescriptionRef m_FormatDesc;
    NSView* m_StreamView;
    NSTextField* m_OverlayTextFields[Overlay::OverlayMax];
    CVDisplayLinkRef m_DisplayLink;
    AVColorSpace m_LastAvColorSpace;
    CGColorSpaceRef m_ColorSpace;
    SDL_mutex* m_VsyncMutex;
    SDL_cond* m_VsyncPassed;
};

IFFmpegRenderer* VTRendererFactory::createRenderer() {
    return new VTRenderer();
}
