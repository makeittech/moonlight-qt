#include <Limelight.h>
#include "ffmpeg.h"

#ifdef _WIN32
#include "ffmpeg-renderers/dxva2.h"
#endif

#ifdef __APPLE__
#include "ffmpeg-renderers/vt.h"
#endif

bool FFmpegVideoDecoder::chooseDecoder(
        StreamingPreferences::VideoDecoderSelection vds,
        SDL_Window* window,
        int videoFormat,
        int width, int height,
        AVCodec*& chosenDecoder,
        const AVCodecHWConfig*& chosenHwConfig,
        IFFmpegRenderer*& newRenderer)
{
    if (videoFormat & VIDEO_FORMAT_MASK_H264) {
        chosenDecoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    }
    else if (videoFormat & VIDEO_FORMAT_MASK_H265) {
        chosenDecoder = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    }
    else {
        Q_ASSERT(false);
        chosenDecoder = nullptr;
    }

    if (!chosenDecoder) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to find decoder for format: %x",
                     videoFormat);
        return false;
    }

    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(chosenDecoder, i);
        if (!config || vds == StreamingPreferences::VDS_FORCE_SOFTWARE) {
            // No matching hardware acceleration support.
            // This is not an error.
            chosenHwConfig = nullptr;
            newRenderer = new SdlRenderer();
            if (vds != StreamingPreferences::VDS_FORCE_HARDWARE &&
                    newRenderer->initialize(window, videoFormat, width, height)) {
                return true;
            }
            else {
                delete newRenderer;
                newRenderer = nullptr;
                return false;
            }
        }

        if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
            continue;
        }

        // Look for acceleration types we support
        switch (config->device_type) {
#ifdef _WIN32
        case AV_HWDEVICE_TYPE_DXVA2:
            newRenderer = new DXVA2Renderer();
            break;
#endif
#ifdef __APPLE__
        case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
            newRenderer = VTRendererFactory::createRenderer();
            break;
#endif
        default:
            continue;
        }

        if (newRenderer->initialize(window, videoFormat, width, height)) {
            chosenHwConfig = config;
            return true;
        }
        else {
            // Failed to initialize
            delete newRenderer;
            newRenderer = nullptr;
        }
    }
}

bool FFmpegVideoDecoder::isHardwareAccelerated()
{
    return m_HwDecodeCfg != nullptr;
}

FFmpegVideoDecoder::FFmpegVideoDecoder()
    : m_VideoDecoderCtx(nullptr),
      m_DecodeBuffer(1024 * 1024, 0),
      m_HwDecodeCfg(nullptr),
      m_Renderer(nullptr)
{
    av_init_packet(&m_Pkt);

    // Use linear filtering when renderer scaling is required
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
}

FFmpegVideoDecoder::~FFmpegVideoDecoder()
{
    avcodec_close(m_VideoDecoderCtx);
    av_free(m_VideoDecoderCtx);
    m_VideoDecoderCtx = nullptr;

    m_HwDecodeCfg = nullptr;

    delete m_Renderer;
    m_Renderer = nullptr;
}

bool FFmpegVideoDecoder::initialize(
        StreamingPreferences::VideoDecoderSelection vds,
        SDL_Window* window,
        int videoFormat,
        int width,
        int height,
        int)
{
    AVCodec* decoder;

    if (!chooseDecoder(vds, window, videoFormat, width, height,
                       decoder, m_HwDecodeCfg, m_Renderer)) {
        // Error logged in chooseDecoder()
        return false;
    }

    m_VideoDecoderCtx = avcodec_alloc_context3(decoder);
    if (!m_VideoDecoderCtx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to allocate video decoder context");
        return false;
    }

    // Always request low delay decoding
    m_VideoDecoderCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    // Enable slice multi-threading for software decoding
    if (!m_HwDecodeCfg) {
        m_VideoDecoderCtx->thread_type = FF_THREAD_SLICE;
        m_VideoDecoderCtx->thread_count = qMin(MAX_SLICES, SDL_GetCPUCount());
    }
    else {
        // No threading for HW decode
        m_VideoDecoderCtx->thread_count = 1;
    }

    // Setup decoding parameters
    m_VideoDecoderCtx->width = width;
    m_VideoDecoderCtx->height = height;
    m_VideoDecoderCtx->pix_fmt = AV_PIX_FMT_YUV420P; // FIXME: HDR

    // Allow the renderer to attach data to this decoder
    if (!m_Renderer->prepareDecoderContext(m_VideoDecoderCtx)) {
        return false;
    }

    int err = avcodec_open2(m_VideoDecoderCtx, decoder, nullptr);
    if (err < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to open decoder for format: %x",
                     videoFormat);
        return false;
    }

    return true;
}

int FFmpegVideoDecoder::submitDecodeUnit(PDECODE_UNIT du)
{
    PLENTRY entry = du->bufferList;
    int err;

    if (du->fullLength + AV_INPUT_BUFFER_PADDING_SIZE > m_DecodeBuffer.length()) {
        m_DecodeBuffer = QByteArray(du->fullLength + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    }

    int offset = 0;
    while (entry != nullptr) {
        memcpy(&m_DecodeBuffer.data()[offset],
               entry->data,
               entry->length);
        offset += entry->length;
        entry = entry->next;
    }

    SDL_assert(offset == du->fullLength);

    m_Pkt.data = reinterpret_cast<uint8_t*>(m_DecodeBuffer.data());
    m_Pkt.size = du->fullLength;

    err = avcodec_send_packet(m_VideoDecoderCtx, &m_Pkt);
    if (err < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Decoding failed: %d", err);
        char errorstring[512];
        av_strerror(err, errorstring, sizeof(errorstring));
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Decoding failed: %s", errorstring);
        return DR_NEED_IDR;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        // Failed to allocate a frame but we did submit,
        // so we can return DR_OK
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to allocate frame");
        return DR_OK;
    }

    err = avcodec_receive_frame(m_VideoDecoderCtx, frame);
    if (err == 0) {
        SDL_Event event;

        event.type = SDL_USEREVENT;
        event.user.code = SDL_CODE_FRAME_READY;
        event.user.data1 = frame;

        // The main thread will handle freeing this
        SDL_PushEvent(&event);
    }
    else {
        av_frame_free(&frame);
    }

    return DR_OK;
}

// Called on main thread
void FFmpegVideoDecoder::renderFrame(SDL_UserEvent* event)
{
    AVFrame* frame = reinterpret_cast<AVFrame*>(event->data1);
    m_Renderer->renderFrame(frame);
    av_frame_free(&frame);
}

// Called on main thread
void FFmpegVideoDecoder::dropFrame(SDL_UserEvent* event)
{
    AVFrame* frame = reinterpret_cast<AVFrame*>(event->data1);
    av_frame_free(&frame);
}
