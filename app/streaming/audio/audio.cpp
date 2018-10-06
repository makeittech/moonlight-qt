#include "../session.h"
#include "renderers/renderer.h"

#ifndef Q_OS_LINUX
#include "renderers/soundioaudiorenderer.h"
#else
#include "renderers/sdl.h"
#endif

#include <Limelight.h>

IAudioRenderer* Session::createAudioRenderer()
{
#ifndef Q_OS_LINUX
    return new SoundIoAudioRenderer();
#else
    return new SdlAudioRenderer();
#endif
}

bool Session::testAudio(int audioConfiguration)
{
    IAudioRenderer* audioRenderer;

    audioRenderer = createAudioRenderer();
    if (audioRenderer == nullptr) {
        return false;
    }

    // Build a fake OPUS_MULTISTREAM_CONFIGURATION to give
    // the renderer the channel count and sample rate.
    OPUS_MULTISTREAM_CONFIGURATION opusConfig = {};
    opusConfig.sampleRate = 48000;

    switch (audioConfiguration)
    {
    case AUDIO_CONFIGURATION_STEREO:
        opusConfig.channelCount = 2;
        break;
    case AUDIO_CONFIGURATION_51_SURROUND:
        opusConfig.channelCount = 6;
        break;
    default:
        SDL_assert(false);
        return false;
    }

    bool ret = audioRenderer->prepareForPlayback(&opusConfig);

    delete audioRenderer;

    return ret;
}

int Session::arInit(int /* audioConfiguration */,
                    const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
                    void* /* arContext */, int /* arFlags */)
{
    int error;

    SDL_memcpy(&s_ActiveSession->m_AudioConfig, opusConfig, sizeof(*opusConfig));

    s_ActiveSession->m_OpusDecoder =
            opus_multistream_decoder_create(opusConfig->sampleRate,
                                            opusConfig->channelCount,
                                            opusConfig->streams,
                                            opusConfig->coupledStreams,
                                            opusConfig->mapping,
                                            &error);
    if (s_ActiveSession->m_OpusDecoder == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create decoder: %d",
                     error);
        return -1;
    }

    s_ActiveSession->m_AudioRenderer = s_ActiveSession->createAudioRenderer();
    if (!s_ActiveSession->m_AudioRenderer->prepareForPlayback(opusConfig)) {
        delete s_ActiveSession->m_AudioRenderer;
        opus_multistream_decoder_destroy(s_ActiveSession->m_OpusDecoder);
        return -2;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Audio stream has %d channels",
                opusConfig->channelCount);

    return 0;
}

void Session::arCleanup()
{
    delete s_ActiveSession->m_AudioRenderer;
    s_ActiveSession->m_AudioRenderer = nullptr;

    opus_multistream_decoder_destroy(s_ActiveSession->m_OpusDecoder);
    s_ActiveSession->m_OpusDecoder = nullptr;
}

void Session::arDecodeAndPlaySample(char* sampleData, int sampleLength)
{
    int samplesDecoded;

    // Set this thread to high priority to reduce
    // the chance of missing our sample delivery time
    if (s_ActiveSession->m_AudioSampleCount == 0) {
        if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to set audio thread to high priority: %s",
                        SDL_GetError());
        }
    }

    s_ActiveSession->m_AudioSampleCount++;

    if (s_ActiveSession->m_AudioRenderer != nullptr) {
        samplesDecoded = opus_multistream_decode(s_ActiveSession->m_OpusDecoder,
                                                 (unsigned char*)sampleData,
                                                 sampleLength,
                                                 s_ActiveSession->m_OpusDecodeBuffer,
                                                 SAMPLES_PER_FRAME,
                                                 0);
        if (samplesDecoded > 0) {
            if (!s_ActiveSession->m_AudioRenderer->submitAudio(s_ActiveSession->m_OpusDecodeBuffer,
                                                               static_cast<int>(
                                                                   sizeof(short) *
                                                                   samplesDecoded *
                                                                   s_ActiveSession->m_AudioConfig.channelCount))) {

                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Reinitializing audio renderer after failure");

                delete s_ActiveSession->m_AudioRenderer;
                s_ActiveSession->m_AudioRenderer = nullptr;
            }
        }
    }

    // Only try to recreate the audio renderer every 200 samples (1 second)
    // to avoid thrashing if the audio device is unavailable. It is
    // safe to reinitialize here because we can't be torn down while
    // the audio decoder/playback thread is still alive.
    if (s_ActiveSession->m_AudioRenderer == nullptr && (s_ActiveSession->m_AudioSampleCount % 200) == 0) {
        s_ActiveSession->m_AudioRenderer = s_ActiveSession->createAudioRenderer();
        if (!s_ActiveSession->m_AudioRenderer->prepareForPlayback(&s_ActiveSession->m_AudioConfig)) {
            delete s_ActiveSession->m_AudioRenderer;
            s_ActiveSession->m_AudioRenderer = nullptr;
        }
    }
}
