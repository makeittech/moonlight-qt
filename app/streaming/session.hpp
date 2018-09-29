#pragma once

#include <QSemaphore>

#include <Limelight.h>
#include <opus_multistream.h>
#include "backend/computermanager.h"
#include "settings/streamingpreferences.h"
#include "input.hpp"
#include "video/decoder.h"
#include "audio/renderers/renderer.h"

class Session : public QObject
{
    Q_OBJECT

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;

public:
    explicit Session(NvComputer* computer, NvApp& app, StreamingPreferences *preferences = nullptr);

    Q_INVOKABLE void exec(int displayOriginX, int displayOriginY);

    static
    bool isHardwareDecodeAvailable(StreamingPreferences::VideoDecoderSelection vds,
                                   int videoFormat, int width, int height, int frameRate);

    static
    int getDecoderCapabilities(StreamingPreferences::VideoDecoderSelection vds,
                               int videoFormat, int width, int height, int frameRate);

signals:
    void stageStarting(QString stage);

    void stageFailed(QString stage, long errorCode);

    void connectionStarted();

    void displayLaunchError(QString text);

    void displayLaunchWarning(QString text);

private:
    void initialize();

    bool validateLaunch();

    void emitLaunchWarning(QString text);

    int getDecoderCapabilities();

    IAudioRenderer* createAudioRenderer();

    int detectAudioConfiguration();

    void cleanupAudioRendererOnMainThread();

    bool testAudio(int audioConfiguration);

    void getWindowDimensions(int& x, int& y,
                             int& width, int& height);

    void toggleFullscreen();

    void updateOptimalWindowDisplayMode();

    static
    bool chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                       SDL_Window* window, int videoFormat, int width, int height,
                       int frameRate, bool enableVsync, IVideoDecoder*& chosenDecoder);

    static
    void clStageStarting(int stage);

    static
    void clStageFailed(int stage, long errorCode);

    static
    void clConnectionTerminated(long errorCode);

    static
    void clLogMessage(const char* format, ...);

    static
    int arInit(int audioConfiguration,
               const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
               void* arContext, int arFlags);

    static
    void arCleanup();

    static
    void arDecodeAndPlaySample(char* sampleData, int sampleLength);

    static
    int drSetup(int videoFormat, int width, int height, int frameRate, void*, int);

    static
    void drCleanup();

    static
    int drSubmitDecodeUnit(PDECODE_UNIT du);

    StreamingPreferences* m_Preferences;
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    NvComputer* m_Computer;
    NvApp m_App;
    SDL_Window* m_Window;
    IVideoDecoder* m_VideoDecoder;
    SDL_SpinLock m_DecoderLock;
    bool m_NeedsIdr;
    bool m_AudioDisabled;
    Uint32 m_FullScreenFlag;
    int m_DisplayOriginX;
    int m_DisplayOriginY;
    bool m_PendingWindowedTransition;

    int m_ActiveVideoFormat;
    int m_ActiveVideoWidth;
    int m_ActiveVideoHeight;
    int m_ActiveVideoFrameRate;

    OpusMSDecoder* m_OpusDecoder;
    short m_OpusDecodeBuffer[MAX_CHANNELS * SAMPLES_PER_FRAME];
    IAudioRenderer* m_AudioRenderer;
    OPUS_MULTISTREAM_CONFIGURATION m_AudioConfig;
    SDL_SpinLock m_AudioRendererLock;

    static AUDIO_RENDERER_CALLBACKS k_AudioCallbacks;
    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;
    static Session* s_ActiveSession;
    static QSemaphore s_ActiveSessionSemaphore;
};
