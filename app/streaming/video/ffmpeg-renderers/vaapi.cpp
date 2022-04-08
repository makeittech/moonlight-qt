#include <QString>

#include "vaapi.h"
#include "utils.h"
#include <streaming/streamutils.h>

#include <SDL_syswm.h>

#include <unistd.h>
#include <fcntl.h>

VAAPIRenderer::VAAPIRenderer()
    : m_HwContext(nullptr),
      m_BlacklistedForDirectRendering(false)
{
#ifdef HAVE_EGL
    m_PrimeDescriptor.num_layers = 0;
    m_PrimeDescriptor.num_objects = 0;
    m_EGLExtDmaBuf = false;

    m_eglCreateImage = nullptr;
    m_eglCreateImageKHR = nullptr;
    m_eglDestroyImage = nullptr;
    m_eglDestroyImageKHR = nullptr;
#endif
}

VAAPIRenderer::~VAAPIRenderer()
{
    if (m_HwContext != nullptr) {
        AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
        AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;

        // Hold onto this VADisplay since we'll need it to uninitialize VAAPI
        VADisplay display = vaDeviceContext->display;

        av_buffer_unref(&m_HwContext);

        if (display) {
            vaTerminate(display);
        }
    }
}

VADisplay
VAAPIRenderer::openDisplay(SDL_Window* window)
{
    SDL_SysWMinfo info;
    VADisplay display;

    SDL_VERSION(&info.version);

    if (!SDL_GetWindowWMInfo(window, &info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_GetWindowWMInfo() failed: %s",
                     SDL_GetError());
        return nullptr;
    }

    m_WindowSystem = info.subsystem;
    if (info.subsystem == SDL_SYSWM_X11) {
#ifdef HAVE_LIBVA_X11
        m_XWindow = info.info.x11.window;
        display = vaGetDisplay(info.info.x11.display);
        if (display == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unable to open X11 display for VAAPI");
            return nullptr;
        }
#else
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Moonlight not compiled with VAAPI X11 support!");
        return nullptr;
#endif
    }
    else if (info.subsystem == SDL_SYSWM_WAYLAND) {
#ifdef HAVE_LIBVA_WAYLAND
        display = vaGetDisplayWl(info.info.wl.display);
        if (display == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unable to open Wayland display for VAAPI");
            return nullptr;
        }
#else
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Moonlight not compiled with VAAPI Wayland support!");
        return nullptr;
#endif
    }
#if defined(SDL_VIDEO_DRIVER_KMSDRM) && defined(HAVE_LIBVA_DRM) && SDL_VERSION_ATLEAST(2, 0, 15)
    else if (info.subsystem == SDL_SYSWM_KMSDRM) {
        SDL_assert(info.info.kmsdrm.drm_fd >= 0);
        display = vaGetDisplayDRM(info.info.kmsdrm.drm_fd);
        if (display == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Unable to open DRM display for VAAPI");
            return nullptr;
        }
    }
#endif
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unsupported VAAPI rendering subsystem: %d",
                     info.subsystem);
        return nullptr;
    }

    return display;
}

bool
VAAPIRenderer::initialize(PDECODER_PARAMETERS params)
{
    int err;

    m_VideoFormat = params->videoFormat;
    m_VideoWidth = params->width;
    m_VideoHeight = params->height;

    SDL_GetWindowSize(params->window, &m_DisplayWidth, &m_DisplayHeight);

    m_HwContext = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
    if (!m_HwContext) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to allocate VAAPI context");
        return false;
    }

    AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;

    vaDeviceContext->display = openDisplay(params->window);
    if (vaDeviceContext->display == nullptr) {
        // openDisplay() logs the error
        return false;
    }

    int major, minor;
    VAStatus status;
    bool setPathVar = false;

    for (;;) {
        status = vaInitialize(vaDeviceContext->display, &major, &minor);
        if (status != VA_STATUS_SUCCESS && qEnvironmentVariableIsEmpty("LIBVA_DRIVER_NAME")) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Trying fallback VAAPI driver names");

            // It would be nice to use vaSetDriverName() here, but there's no way to unset
            // it and get back to the default driver selection logic once we've overridden
            // the driver name using that API. As a result, we must use LIBVA_DRIVER_NAME.

            if (status != VA_STATUS_SUCCESS) {
                // The iHD driver supports newer hardware like Ice Lake and Comet Lake.
                // It should be picked by default on those platforms, but that doesn't
                // always seem to be the case for some reason.
                qputenv("LIBVA_DRIVER_NAME", "iHD");
                status = vaInitialize(vaDeviceContext->display, &major, &minor);
            }

            if (status != VA_STATUS_SUCCESS) {
                // The Iris driver in Mesa 20.0 returns a bogus VA driver (iris_drv_video.so)
                // even though the correct driver is still i965. If we hit this path, we'll
                // explicitly try i965 to handle this case.
                qputenv("LIBVA_DRIVER_NAME", "i965");
                status = vaInitialize(vaDeviceContext->display, &major, &minor);
            }

            if (status != VA_STATUS_SUCCESS) {
                // The RadeonSI driver is compatible with XWayland but can't be detected by libva
                // so try it too if all else fails.
                qputenv("LIBVA_DRIVER_NAME", "radeonsi");
                status = vaInitialize(vaDeviceContext->display, &major, &minor);
            }

            if (status != VA_STATUS_SUCCESS) {
                // Unset LIBVA_DRIVER_NAME if none of the drivers we tried worked. This ensures
                // we will get a fresh start using the default driver selection behavior after
                // setting LIBVA_DRIVERS_PATH in the code below.
                qunsetenv("LIBVA_DRIVER_NAME");
            }
        }

        if (status == VA_STATUS_SUCCESS) {
            // Success!
            break;
        }

        if (qEnvironmentVariableIsEmpty("LIBVA_DRIVERS_PATH")) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Trying fallback VAAPI driver paths");

            qputenv("LIBVA_DRIVERS_PATH",
        #if Q_PROCESSOR_WORDSIZE == 8
                    "/usr/lib64/dri:" // Fedora x86_64
                    "/usr/lib64/va/drivers:" // Gentoo x86_64
        #endif
                    "/usr/lib/dri:" // Arch i386 & x86_64, Fedora i386
                    "/usr/lib/va/drivers:" // Gentoo i386
        #if defined(Q_PROCESSOR_X86_64)
                    "/usr/lib/x86_64-linux-gnu/dri:" // Ubuntu/Debian x86_64
        #elif defined(Q_PROCESSOR_X86_32)
                    "/usr/lib/i386-linux-gnu/dri:" // Ubuntu/Debian i386
        #endif
                    );
           setPathVar = true;
        }
        else {
            if (setPathVar) {
                // Unset LIBVA_DRIVERS_PATH if we set it ourselves
                // and we didn't find any working VAAPI drivers.
                qunsetenv("LIBVA_DRIVERS_PATH");
            }

            // Give up
            break;
        }
    }

    if (status != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to initialize VAAPI: %d",
                     status);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Initialized VAAPI %d.%d",
                major, minor);

    const char* vendorString = vaQueryVendorString(vaDeviceContext->display);
    QString vendorStr(vendorString);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Driver: %s",
                vendorString ? vendorString : "<unknown>");

    // Older versions of the Gallium VAAPI driver have a nasty memory leak that
    // causes memory to be leaked for each submitted frame. I believe this is
    // resolved in the libva2 drivers (VAAPI 1.x).
    // If we're using Wayland, we have no choice but to use VAAPI because VDPAU
    // is only supported under X11 or XWayland.
    if (major == 0 && qgetenv("FORCE_VAAPI") != "1" && !WMUtils::isRunningWayland()) {
        if (vendorStr.contains("AMD", Qt::CaseInsensitive) ||
                vendorStr.contains("Radeon", Qt::CaseInsensitive)) {
            // Fail and let VDPAU pick this up
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Avoiding VAAPI on AMD driver");
            return false;
        }
    }

    if (WMUtils::isRunningWayland()) {
        // The iHD VAAPI driver can initialize on XWayland but it crashes in
        // vaPutSurface() so we must also not directly render on XWayland.
        m_BlacklistedForDirectRendering = vendorStr.contains("iHD");
    }

    // This will populate the driver_quirks
    err = av_hwdevice_ctx_init(m_HwContext);
    if (err < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to initialize VAAPI context: %d",
                     err);
        return false;
    }

    // This quirk is set for the VDPAU wrapper which doesn't work with our VAAPI renderer
    if (vaDeviceContext->driver_quirks & AV_VAAPI_DRIVER_QUIRK_SURFACE_ATTRIBUTES) {
        // Fail and let our VDPAU renderer pick this up
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Avoiding VDPAU wrapper for VAAPI decoding");
        return false;
    }

    return true;
}

bool
VAAPIRenderer::prepareDecoderContext(AVCodecContext* context, AVDictionary**)
{
    context->hw_device_ctx = av_buffer_ref(m_HwContext);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using VAAPI accelerated renderer on %s",
                SDL_GetCurrentVideoDriver());

    return true;
}

bool
VAAPIRenderer::needsTestFrame()
{
    // We need a test frame to see if this VAAPI driver
    // supports the profile used for streaming
    return true;
}

bool
VAAPIRenderer::isDirectRenderingSupported()
{
    if (qgetenv("VAAPI_FORCE_DIRECT") == "1") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using direct rendering due to environment variable");
        return true;
    }
    else if (qgetenv("VAAPI_FORCE_INDIRECT") == "1") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using indirect rendering due to environment variable");
        return false;
    }

    // We only support direct rendering on X11 with VAEntrypointVideoProc support
    if (m_WindowSystem != SDL_SYSWM_X11 || m_BlacklistedForDirectRendering) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using indirect rendering due to WM or blacklist");
        return false;
    }
    else if (m_VideoFormat == VIDEO_FORMAT_H265_MAIN10) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using indirect rendering for 10-bit video");
        return false;
    }

    AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;
    VAEntrypoint entrypoints[vaMaxNumEntrypoints(vaDeviceContext->display)];
    int entrypointCount;
    VAStatus status = vaQueryConfigEntrypoints(vaDeviceContext->display, VAProfileNone, entrypoints, &entrypointCount);
    if (status == VA_STATUS_SUCCESS) {
        for (int i = 0; i < entrypointCount; i++) {
            // Without VAEntrypointVideoProc support, the driver will crash inside vaPutSurface()
            if (entrypoints[i] == VAEntrypointVideoProc) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Using direct rendering with VAEntrypointVideoProc");
                return true;
            }
        }
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Using indirect rendering due to lack of VAEntrypointVideoProc");
    return false;
}

int VAAPIRenderer::getDecoderColorspace()
{
    // Gallium drivers don't support Rec 709 yet - https://gitlab.freedesktop.org/mesa/mesa/issues/1915
    // Intel-vaapi-driver defaults to Rec 601 - https://github.com/intel/intel-vaapi-driver/blob/021bcb79d1bd873bbd9fbca55f40320344bab866/src/i965_output_dri.c#L186
    return COLORSPACE_REC_601;
}

void
VAAPIRenderer::renderFrame(AVFrame* frame)
{
    VASurfaceID surface = (VASurfaceID)(uintptr_t)frame->data[3];
    AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;

    SDL_Rect src, dst;
    src.x = src.y = 0;
    src.w = m_VideoWidth;
    src.h = m_VideoHeight;
    dst.x = dst.y = 0;
    dst.w = m_DisplayWidth;
    dst.h = m_DisplayHeight;

    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

    if (m_WindowSystem == SDL_SYSWM_X11) {
#ifdef HAVE_LIBVA_X11
        unsigned int flags = 0;

        // NB: Not all VAAPI drivers respect these flags. Many drivers
        // just ignore them and do the color conversion as Rec 601.
        switch (frame->colorspace) {
        case AVCOL_SPC_BT709:
            flags |= VA_SRC_BT709;
            break;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M:
            flags |= VA_SRC_BT601;
            break;
        case AVCOL_SPC_SMPTE240M:
            flags |= VA_SRC_SMPTE_240;
            break;
        default:
            // Unknown colorspace
            SDL_assert(false);
            break;
        }

        vaPutSurface(vaDeviceContext->display,
                     surface,
                     m_XWindow,
                     0, 0,
                     m_VideoWidth, m_VideoHeight,
                     dst.x, dst.y,
                     dst.w, dst.h,
                     NULL, 0, flags);
#endif
    }
    else if (m_WindowSystem == SDL_SYSWM_WAYLAND) {
        // We don't support direct rendering on Wayland, so we should
        // never get called there. Many common Wayland compositors don't
        // support YUV surfaces, so direct rendering would fail.
        SDL_assert(false);
    }
    else {
        // We don't accept anything else in initialize().
        SDL_assert(false);
    }
}

#if defined(HAVE_EGL) || defined(HAVE_DRM)

// Ensure that vaExportSurfaceHandle() is supported by the VA-API driver
bool
VAAPIRenderer::canExportSurfaceHandle(int layerTypeFlag) {
    AVHWDeviceContext* deviceContext = (AVHWDeviceContext*)m_HwContext->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)deviceContext->hwctx;
    VASurfaceID surfaceId;
    VAStatus st;
    VADRMPRIMESurfaceDescriptor descriptor;
    VASurfaceAttrib attrs[2];
    int attributeCount = 0;

    if (qgetenv("VAAPI_FORCE_DIRECT") == "1") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Using direct rendering due to environment variable");
        return false;
    }

    // FFmpeg handles setting these quirk flags for us
    if (!(vaDeviceContext->driver_quirks & AV_VAAPI_DRIVER_QUIRK_ATTRIB_MEMTYPE)) {
        attrs[attributeCount].type = VASurfaceAttribMemoryType;
        attrs[attributeCount].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attrs[attributeCount].value.type = VAGenericValueTypeInteger;
        attrs[attributeCount].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_VA;
        attributeCount++;
    }

    // These attributes are required for i965 to create a surface that can
    // be successfully exported via vaExportSurfaceHandle(). iHD doesn't
    // need these, but it doesn't seem to hurt either.
    attrs[attributeCount].type = VASurfaceAttribPixelFormat;
    attrs[attributeCount].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrs[attributeCount].value.type = VAGenericValueTypeInteger;
    attrs[attributeCount].value.value.i = (m_VideoFormat == VIDEO_FORMAT_H265_MAIN10) ?
                VA_FOURCC_P010 : VA_FOURCC_NV12;
    attributeCount++;

    st = vaCreateSurfaces(vaDeviceContext->display,
                          m_VideoFormat == VIDEO_FORMAT_H265_MAIN10 ?
                              VA_RT_FORMAT_YUV420_10 : VA_RT_FORMAT_YUV420,
                          1280,
                          720,
                          &surfaceId,
                          1,
                          attrs,
                          attributeCount);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaCreateSurfaces() failed: %d", st);
        return false;
    }

    st = vaExportSurfaceHandle(vaDeviceContext->display,
                               surfaceId,
                               VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                               VA_EXPORT_SURFACE_READ_ONLY | layerTypeFlag,
                               &descriptor);

    vaDestroySurfaces(vaDeviceContext->display, &surfaceId, 1);

    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaExportSurfaceHandle() failed: %d", st);
        return false;
    }

    for (size_t i = 0; i < descriptor.num_objects; ++i) {
        close(descriptor.objects[i].fd);
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "VAAPI driver supports exporting DRM PRIME surface handles with %s layers",
                layerTypeFlag == VA_EXPORT_SURFACE_COMPOSED_LAYERS ? "composed" : "separate");
    return true;
}

#endif

#ifdef HAVE_EGL

bool
VAAPIRenderer::canExportEGL() {
    // Our EGL export logic requires exporting separate layers
    return canExportSurfaceHandle(VA_EXPORT_SURFACE_SEPARATE_LAYERS);
}

AVPixelFormat VAAPIRenderer::getEGLImagePixelFormat() {
    return m_VideoFormat == VIDEO_FORMAT_H265_MAIN10 ?
                AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
}

bool
VAAPIRenderer::initializeEGL(EGLDisplay,
                             const EGLExtensions &ext) {
    if (!ext.isSupported("EGL_EXT_image_dma_buf_import")) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VAAPI-EGL: DMABUF unsupported");
        return false;
    }
    m_EGLExtDmaBuf = ext.isSupported("EGL_EXT_image_dma_buf_import_modifiers");

    // NB: eglCreateImage() and eglCreateImageKHR() have slightly different definitions
    m_eglCreateImage = (typeof(m_eglCreateImage))eglGetProcAddress("eglCreateImage");
    m_eglCreateImageKHR = (typeof(m_eglCreateImageKHR))eglGetProcAddress("eglCreateImageKHR");
    m_eglDestroyImage = (typeof(m_eglDestroyImage))eglGetProcAddress("eglDestroyImage");
    m_eglDestroyImageKHR = (typeof(m_eglDestroyImageKHR))eglGetProcAddress("eglDestroyImageKHR");

    if (!(m_eglCreateImage && m_eglDestroyImage) &&
            !(m_eglCreateImageKHR && m_eglDestroyImageKHR)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Missing eglCreateImage()/eglDestroyImage() in EGL driver");
        return false;
    }

    return true;
}

ssize_t
VAAPIRenderer::exportEGLImages(AVFrame *frame, EGLDisplay dpy,
                               EGLImage images[EGL_MAX_PLANES]) {
    ssize_t count = 0;
    auto hwFrameCtx = (AVHWFramesContext*)frame->hw_frames_ctx->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)hwFrameCtx->device_ctx->hwctx;

    VASurfaceID surface_id = (VASurfaceID)(uintptr_t)frame->data[3];
    VAStatus st = vaExportSurfaceHandle(vaDeviceContext->display,
                                        surface_id,
                                        VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                        VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS,
                                        &m_PrimeDescriptor);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaExportSurfaceHandle failed: %d", st);
        return -1;
    }

    SDL_assert(m_PrimeDescriptor.num_layers <= EGL_MAX_PLANES);

    st = vaSyncSurface(vaDeviceContext->display, surface_id);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaSyncSurface failed: %d", st);
        goto sync_fail;
    }

    for (size_t i = 0; i < m_PrimeDescriptor.num_layers; ++i) {
        const auto &layer = m_PrimeDescriptor.layers[i];

        // Max 30 attributes (1 key + 1 value for each)
        const int EGL_ATTRIB_COUNT = 30 * 2;
        EGLAttrib attribs[EGL_ATTRIB_COUNT] = {
            EGL_LINUX_DRM_FOURCC_EXT, layer.drm_format,
            EGL_WIDTH, i == 0 ? frame->width : frame->width / 2,
            EGL_HEIGHT, i == 0 ? frame->height : frame->height / 2,
        };

        int attribIndex = 6;
        for (size_t j = 0; j < layer.num_planes; j++) {
            const auto &object = m_PrimeDescriptor.objects[layer.object_index[j]];

            switch (j) {
            case 0:
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE0_FD_EXT;
                attribs[attribIndex++] = object.fd;
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
                attribs[attribIndex++] = layer.offset[0];
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
                attribs[attribIndex++] = layer.pitch[0];
                if (m_EGLExtDmaBuf) {
                    attribs[attribIndex++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
                    attribs[attribIndex++] = (EGLint)(object.drm_format_modifier & 0xFFFFFFFF);
                    attribs[attribIndex++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
                    attribs[attribIndex++] = (EGLint)(object.drm_format_modifier >> 32);
                }
                break;

            case 1:
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE1_FD_EXT;
                attribs[attribIndex++] = object.fd;
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
                attribs[attribIndex++] = layer.offset[1];
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
                attribs[attribIndex++] = layer.pitch[1];
                if (m_EGLExtDmaBuf) {
                    attribs[attribIndex++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
                    attribs[attribIndex++] = (EGLint)(object.drm_format_modifier & 0xFFFFFFFF);
                    attribs[attribIndex++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
                    attribs[attribIndex++] = (EGLint)(object.drm_format_modifier >> 32);
                }
                break;

            case 2:
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE2_FD_EXT;
                attribs[attribIndex++] = object.fd;
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
                attribs[attribIndex++] = layer.offset[2];
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
                attribs[attribIndex++] = layer.pitch[2];
                if (m_EGLExtDmaBuf) {
                    attribs[attribIndex++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
                    attribs[attribIndex++] = (EGLint)(object.drm_format_modifier & 0xFFFFFFFF);
                    attribs[attribIndex++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
                    attribs[attribIndex++] = (EGLint)(object.drm_format_modifier >> 32);
                }
                break;

            case 3:
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE3_FD_EXT;
                attribs[attribIndex++] = object.fd;
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE3_OFFSET_EXT;
                attribs[attribIndex++] = layer.offset[3];
                attribs[attribIndex++] = EGL_DMA_BUF_PLANE3_PITCH_EXT;
                attribs[attribIndex++] = layer.pitch[3];
                if (m_EGLExtDmaBuf) {
                    attribs[attribIndex++] = EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT;
                    attribs[attribIndex++] = (EGLint)(object.drm_format_modifier & 0xFFFFFFFF);
                    attribs[attribIndex++] = EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT;
                    attribs[attribIndex++] = (EGLint)(object.drm_format_modifier >> 32);
                }
                break;

            default:
                Q_UNREACHABLE();
            }
        }

        // Terminate the attribute list
        attribs[attribIndex++] = EGL_NONE;
        SDL_assert(attribIndex <= EGL_ATTRIB_COUNT);

        if (m_eglCreateImage) {
            images[i] = m_eglCreateImage(dpy, EGL_NO_CONTEXT,
                                         EGL_LINUX_DMA_BUF_EXT,
                                         nullptr, attribs);
            if (!images[i]) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "eglCreateImage() Failed: %d", eglGetError());
                goto create_image_fail;
            }
        }
        else {
            // Cast the EGLAttrib array elements to EGLint for the KHR extension
            EGLint intAttribs[EGL_ATTRIB_COUNT];
            for (int i = 0; i < attribIndex; i++) {
                intAttribs[i] = (EGLint)attribs[i];
            }

            images[i] = m_eglCreateImageKHR(dpy, EGL_NO_CONTEXT,
                                            EGL_LINUX_DMA_BUF_EXT,
                                            nullptr, intAttribs);
            if (!images[i]) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "eglCreateImageKHR() Failed: %d", eglGetError());
                goto create_image_fail;
            }
        }

        ++count;
    }
    return count;

create_image_fail:
    m_PrimeDescriptor.num_layers = count;
sync_fail:
    freeEGLImages(dpy, images);
    return -1;
}

void
VAAPIRenderer::freeEGLImages(EGLDisplay dpy, EGLImage images[EGL_MAX_PLANES]) {
    for (size_t i = 0; i < m_PrimeDescriptor.num_layers; ++i) {
        if (m_eglDestroyImage) {
            m_eglDestroyImage(dpy, images[i]);
        }
        else {
            m_eglDestroyImageKHR(dpy, images[i]);
        }
    }
    for (size_t i = 0; i < m_PrimeDescriptor.num_objects; ++i) {
        close(m_PrimeDescriptor.objects[i].fd);
    }
    m_PrimeDescriptor.num_layers = 0;
    m_PrimeDescriptor.num_objects = 0;
}

#endif

#ifdef HAVE_DRM

bool VAAPIRenderer::canExportDrmPrime()
{
    // Our DRM renderer requires composed layers
    return canExportSurfaceHandle(VA_EXPORT_SURFACE_COMPOSED_LAYERS);
}

bool VAAPIRenderer::mapDrmPrimeFrame(AVFrame* frame, AVDRMFrameDescriptor* drmDescriptor)
{
    auto hwFrameCtx = (AVHWFramesContext*)frame->hw_frames_ctx->data;
    AVVAAPIDeviceContext* vaDeviceContext = (AVVAAPIDeviceContext*)hwFrameCtx->device_ctx->hwctx;
    VASurfaceID vaSurfaceId = (VASurfaceID)(uintptr_t)frame->data[3];
    VADRMPRIMESurfaceDescriptor vaDrmPrimeDescriptor;

    VAStatus st = vaExportSurfaceHandle(vaDeviceContext->display,
                                        vaSurfaceId,
                                        VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                        VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS,
                                        &vaDrmPrimeDescriptor);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaExportSurfaceHandle() failed: %d", st);
        return false;
    }

    st = vaSyncSurface(vaDeviceContext->display, vaSurfaceId);
    if (st != VA_STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "vaSyncSurface() failed: %d", st);
        for (uint32_t i = 0; i < vaDrmPrimeDescriptor.num_objects; i++) {
            close(vaDrmPrimeDescriptor.objects[i].fd);
        }
        return false;
    }

    // Map our VADRMPRIMESurfaceDescriptor to the AVDRMFrameDescriptor our caller wants
    drmDescriptor->nb_objects = vaDrmPrimeDescriptor.num_objects;
    for (uint32_t i = 0; i < vaDrmPrimeDescriptor.num_objects; i++) {
        drmDescriptor->objects[i].fd = vaDrmPrimeDescriptor.objects[i].fd;
        drmDescriptor->objects[i].size = vaDrmPrimeDescriptor.objects[i].size;
        drmDescriptor->objects[i].format_modifier = vaDrmPrimeDescriptor.objects[i].drm_format_modifier;
    }
    drmDescriptor->nb_layers = vaDrmPrimeDescriptor.num_layers;
    for (uint32_t i = 0; i < vaDrmPrimeDescriptor.num_layers; i++) {
        drmDescriptor->layers[i].format = vaDrmPrimeDescriptor.layers[i].drm_format;
        drmDescriptor->layers[i].nb_planes = vaDrmPrimeDescriptor.layers[i].num_planes;
        for (uint32_t j = 0; j < vaDrmPrimeDescriptor.layers[i].num_planes; j++) {
            drmDescriptor->layers[i].planes[j].object_index = vaDrmPrimeDescriptor.layers[i].object_index[j];
            drmDescriptor->layers[i].planes[j].offset = vaDrmPrimeDescriptor.layers[i].offset[j];
            drmDescriptor->layers[i].planes[j].pitch = vaDrmPrimeDescriptor.layers[i].pitch[j];
        }
    }

    return true;
}

void VAAPIRenderer::unmapDrmPrimeFrame(AVDRMFrameDescriptor* drmDescriptor)
{
    for (int i = 0; i < drmDescriptor->nb_objects; i++) {
        close(drmDescriptor->objects[i].fd);
    }
}

#endif
