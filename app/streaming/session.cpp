#include "session.hpp"
#include "settings/streamingpreferences.h"

#include <Limelight.h>
#include <SDL.h>
#include "utils.h"

#include "video/ffmpeg.h"

#include <QRandomGenerator>
#include <QtEndian>
#include <QCoreApplication>
#include <QThreadPool>

CONNECTION_LISTENER_CALLBACKS Session::k_ConnCallbacks = {
    Session::clStageStarting,
    nullptr,
    Session::clStageFailed,
    nullptr,
    Session::clConnectionTerminated,
    nullptr,
    nullptr,
    Session::clLogMessage
};

AUDIO_RENDERER_CALLBACKS Session::k_AudioCallbacks = {
    Session::sdlAudioInit,
    Session::sdlAudioStart,
    Session::sdlAudioStop,
    Session::sdlAudioCleanup,
    Session::sdlAudioDecodeAndPlaySample,
    CAPABILITY_DIRECT_SUBMIT
};

Session* Session::s_ActiveSession;
QSemaphore Session::s_ActiveSessionSemaphore(1);

void Session::clStageStarting(int stage)
{
    // We know this is called on the same thread as LiStartConnection()
    // which happens to be the main thread, so it's cool to interact
    // with the GUI in these callbacks.
    emit s_ActiveSession->stageStarting(QString::fromLocal8Bit(LiGetStageName(stage)));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void Session::clStageFailed(int stage, long errorCode)
{
    // We know this is called on the same thread as LiStartConnection()
    // which happens to be the main thread, so it's cool to interact
    // with the GUI in these callbacks.
    emit s_ActiveSession->stageFailed(QString::fromLocal8Bit(LiGetStageName(stage)), errorCode);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void Session::clConnectionTerminated(long errorCode)
{
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Connection terminated: %ld",
                 errorCode);

    // Push a quit event to the main loop
    SDL_Event event;
    event.type = SDL_QUIT;
    event.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&event);
}

void Session::clLogMessage(const char* format, ...)
{
    va_list ap;

    va_start(ap, format);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION,
                    SDL_LOG_PRIORITY_INFO,
                    format,
                    ap);
    va_end(ap);
}

bool Session::chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                            SDL_Window* window, int videoFormat, int width, int height,
                            int frameRate, IVideoDecoder*& chosenDecoder)
{
    chosenDecoder = new FFmpegVideoDecoder();
    if (chosenDecoder->initialize(vds, window, videoFormat, width, height, frameRate)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "FFmpeg-based video decoder chosen");
        return true;
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to load FFmpeg decoder");
        delete chosenDecoder;
        chosenDecoder = nullptr;
    }

    // If we reach this, we didn't initialize any decoders successfully
    return false;
}

int Session::drSetup(int videoFormat, int width, int height, int frameRate, void *, int)
{
    if (!chooseDecoder(s_ActiveSession->m_Preferences.videoDecoderSelection,
                       s_ActiveSession->m_Window,
                       videoFormat, width, height, frameRate,
                       s_ActiveSession->m_VideoDecoder)) {
        return -1;
    }

    return 0;
}

int Session::drSubmitDecodeUnit(PDECODE_UNIT du)
{
    // Use a lock since we'll be yanking this decoder out
    // from underneath the session when we initiate destruction.
    // We need to destroy the decoder on the main thread to satisfy
    // some API constraints (like DXVA2).
    SDL_AtomicLock(&s_ActiveSession->m_DecoderLock);
    IVideoDecoder* decoder = s_ActiveSession->m_VideoDecoder;
    if (decoder != nullptr) {
        int ret = decoder->submitDecodeUnit(du);
        SDL_AtomicUnlock(&s_ActiveSession->m_DecoderLock);
        return ret;
    }
    else {
        SDL_AtomicUnlock(&s_ActiveSession->m_DecoderLock);
        return DR_OK;
    }
}

bool Session::isHardwareDecodeAvailable(StreamingPreferences::VideoDecoderSelection vds,
                                        int videoFormat, int width, int height, int frameRate)
{
    IVideoDecoder* decoder;

    SDL_Window* window = SDL_CreateWindow("", 0, 0, width, height, SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create window for hardware decode test: %s",
                     SDL_GetError());
        return false;
    }

    if (!chooseDecoder(vds, window, videoFormat, width, height, frameRate, decoder)) {
        SDL_DestroyWindow(window);
        return false;
    }

    SDL_DestroyWindow(window);

    bool ret = decoder->isHardwareAccelerated();
    delete decoder;
    return ret;
}

Session::Session(NvComputer* computer, NvApp& app)
    : m_Computer(computer),
      m_App(app),
      m_Window(nullptr),
      m_VideoDecoder(nullptr),
      m_DecoderLock(0)
{
    LiInitializeVideoCallbacks(&m_VideoCallbacks);
    m_VideoCallbacks.setup = drSetup;
    m_VideoCallbacks.submitDecodeUnit = drSubmitDecodeUnit;

    // Submit for decode without using a separate thread
    m_VideoCallbacks.capabilities |= CAPABILITY_DIRECT_SUBMIT;

    // Slice up to 4 times for parallel decode, once slice per core
    m_VideoCallbacks.capabilities |= CAPABILITY_SLICES_PER_FRAME(qMin(MAX_SLICES, SDL_GetCPUCount()));

    LiInitializeStreamConfiguration(&m_StreamConfig);
    m_StreamConfig.width = m_Preferences.width;
    m_StreamConfig.height = m_Preferences.height;
    m_StreamConfig.fps = m_Preferences.fps;
    m_StreamConfig.bitrate = m_Preferences.bitrateKbps;
    m_StreamConfig.hevcBitratePercentageMultiplier = 75;
    for (unsigned int i = 0; i < sizeof(m_StreamConfig.remoteInputAesKey); i++) {
        m_StreamConfig.remoteInputAesKey[i] =
                (char)(QRandomGenerator::global()->generate() % 256);
    }
    *(int*)m_StreamConfig.remoteInputAesIv = qToBigEndian(QRandomGenerator::global()->generate());
    switch (m_Preferences.audioConfig)
    {
    case StreamingPreferences::AC_AUTO:
        m_StreamConfig.audioConfiguration = sdlDetermineAudioConfiguration();
        break;
    case StreamingPreferences::AC_FORCE_STEREO:
        m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
        break;
    case StreamingPreferences::AC_FORCE_SURROUND:
        m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_51_SURROUND;
        break;
    }

    switch (m_Preferences.videoCodecConfig)
    {
    case StreamingPreferences::VCC_AUTO:
        // TODO: Determine if HEVC is better depending on the decoder
        m_StreamConfig.supportsHevc =
                isHardwareDecodeAvailable(m_Preferences.videoDecoderSelection,
                                          VIDEO_FORMAT_H265,
                                          m_StreamConfig.width,
                                          m_StreamConfig.height,
                                          m_StreamConfig.fps);
        m_StreamConfig.enableHdr = false;
        break;
    case StreamingPreferences::VCC_FORCE_H264:
        m_StreamConfig.supportsHevc = false;
        m_StreamConfig.enableHdr = false;
        break;
    case StreamingPreferences::VCC_FORCE_HEVC:
        m_StreamConfig.supportsHevc = true;
        m_StreamConfig.enableHdr = false;
        break;
    case StreamingPreferences::VCC_FORCE_HEVC_HDR:
        m_StreamConfig.supportsHevc = true;
        m_StreamConfig.enableHdr = true;
        break;
    }

    if (computer->activeAddress == computer->remoteAddress) {
        m_StreamConfig.streamingRemotely = 1;
    }
    else {
        m_StreamConfig.streamingRemotely = 0;
    }

    if (computer->activeAddress == computer->localAddress) {
        m_StreamConfig.packetSize = 1392;
    }
    else {
        m_StreamConfig.packetSize = 1024;
    }
}

bool Session::validateLaunch()
{
    QStringList warningList;

    if (m_StreamConfig.supportsHevc) {
        if (m_Preferences.videoCodecConfig == StreamingPreferences::VCC_FORCE_HEVC ||
                m_Preferences.videoCodecConfig == StreamingPreferences::VCC_FORCE_HEVC_HDR) {
            if (m_Computer->maxLumaPixelsHEVC == 0) {
                emit displayLaunchWarning("Your host PC GPU doesn't support HEVC. "
                                          "A GeForce GTX 900-series (Maxwell) or later GPU is required for HEVC streaming.");
            }
        }
        else if (!isHardwareDecodeAvailable(m_Preferences.videoDecoderSelection,
                                            VIDEO_FORMAT_H265,
                                            m_StreamConfig.width,
                                            m_StreamConfig.height,
                                            m_StreamConfig.fps)) {
            // NOTE: HEVC currently uses only 1 slice regardless of what
            // we provide in CAPABILITY_SLICES_PER_FRAME(), so we should
            // never use it for software decoding (unless common-c starts
            // respecting it for HEVC).
            m_StreamConfig.supportsHevc = false;
        }
    }

    if (m_StreamConfig.enableHdr) {
        // Turn HDR back off unless all criteria are met.
        m_StreamConfig.enableHdr = false;

        // Check that the app supports HDR
        if (!m_App.hdrSupported) {
            emit displayLaunchWarning(m_App.name + " doesn't support HDR10.");
        }
        // Check that the server GPU supports HDR
        else if (!(m_Computer->serverCodecModeSupport & 0x200)) {
            emit displayLaunchWarning("Your host PC GPU doesn't support HDR streaming. "
                                      "A GeForce GTX 1000-series (Pascal) or later GPU is required for HDR streaming.");
        }
        else if (!isHardwareDecodeAvailable(m_Preferences.videoDecoderSelection,
                                            VIDEO_FORMAT_H265_MAIN10,
                                            m_StreamConfig.width,
                                            m_StreamConfig.height,
                                            m_StreamConfig.fps)) {
            emit displayLaunchWarning("Your client PC GPU doesn't support HEVC Main10 decoding for HDR streaming.");
        }
        else {
            // TODO: Also validate display capabilites

            // Validation successful so HDR is good to go
            m_StreamConfig.enableHdr = true;
        }
    }

    if (m_StreamConfig.width >= 3840) {
        // Only allow 4K on GFE 3.x+
        if (m_Computer->gfeVersion.isNull() || m_Computer->gfeVersion.startsWith("2.")) {
            emit displayLaunchWarning("GeForce Experience 3.0 or higher is required for 4K streaming.");

            m_StreamConfig.width = 1920;
            m_StreamConfig.height = 1080;
        }
        // This list is sorted from least to greatest
        else if (m_Computer->displayModes.last().width < 3840 ||
                 (m_Computer->displayModes.last().refreshRate < 60 && m_StreamConfig.fps >= 60)) {
            emit displayLaunchWarning("Your host PC GPU doesn't support 4K streaming. "
                                      "A GeForce GTX 900-series (Maxwell) or later GPU is required for 4K streaming.");

            m_StreamConfig.width = 1920;
            m_StreamConfig.height = 1080;
        }
    }

    // Always allow the launch to proceed for now
    return true;
}


class DeferredSessionCleanupTask : public QRunnable
{
    void run() override
    {
        // Finish cleanup of the connection state
        LiStopConnection();
        if (Session::s_ActiveSession->m_Window != nullptr) {
            SDL_DestroyWindow(Session::s_ActiveSession->m_Window);
        }

        // Allow another session to start now that we're cleaned up
        Session::s_ActiveSession = nullptr;
        Session::s_ActiveSessionSemaphore.release();
    }
};

void Session::exec()
{
    // Check for validation errors/warnings and emit
    // signals for them, if appropriate
    if (!validateLaunch()) {
        return;
    }

    // Manually pump the UI thread for the view
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // Wait for any old session to finish cleanup
    s_ActiveSessionSemaphore.acquire();

    // We're now active
    s_ActiveSession = this;

    // Initialize the gamepad code with our preferences
    StreamingPreferences prefs;
    SdlInputHandler inputHandler(prefs.multiController);

    // The UI should have ensured the old game was already quit
    // if we decide to stream a different game.
    Q_ASSERT(m_Computer->currentGameId == 0 ||
             m_Computer->currentGameId == m_App.id);

    try {
        NvHTTP http(m_Computer->activeAddress);
        if (m_Computer->currentGameId != 0) {
            http.resumeApp(&m_StreamConfig);
        }
        else {
            http.launchApp(m_App.id, &m_StreamConfig,
                           prefs.gameOptimizations,
                           prefs.playAudioOnHost,
                           inputHandler.getAttachedGamepadMask());
        }
    } catch (const GfeHttpResponseException& e) {
        emit displayLaunchError(e.toQString());
        s_ActiveSessionSemaphore.release();
        return;
    }

    int flags = SDL_WINDOW_HIDDEN;
    int width, height;
    if (m_Preferences.fullScreen) {
        SDL_DisplayMode desired, closest;

        SDL_zero(desired);
        desired.w = m_StreamConfig.width;
        desired.h = m_StreamConfig.height;
        desired.refresh_rate = m_StreamConfig.fps;

        if (SDL_GetClosestDisplayMode(0, &desired, &closest)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Closest match for %dx%dx%d is %dx%dx%d",
                        desired.w, desired.h, desired.refresh_rate,
                        closest.w, closest.h, closest.refresh_rate);
            width = closest.w;
            height = closest.h;
        }
        else if (SDL_GetCurrentDisplayMode(0, &closest) == 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Using current display mode: %dx%dx%d",
                        closest.w, closest.h, closest.refresh_rate);
            width = closest.w;
            height = closest.h;
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to get current or closest display mode");
            width = m_StreamConfig.width;
            height = m_StreamConfig.height;
        }

        flags |= SDL_WINDOW_FULLSCREEN;
    }
    else {
        SDL_DisplayMode current;

        width = m_StreamConfig.width;
        height = m_StreamConfig.height;

        if (SDL_GetCurrentDisplayMode(0, &current) == 0) {
            if (current.w <= m_StreamConfig.width || current.h <= m_StreamConfig.height) {
                // If we match or exceed the dimensions of the display, maximize the window
                flags |= SDL_WINDOW_MAXIMIZED;
            }
        }
    }

    m_Window = SDL_CreateWindow("Moonlight",
                                SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED,
                                width,
                                height,
                                flags);
    if (!m_Window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateWindow() failed: %s",
                     SDL_GetError());
        s_ActiveSessionSemaphore.release();
        return;
    }

    QByteArray hostnameStr = m_Computer->activeAddress.toLatin1();
    QByteArray siAppVersion = m_Computer->appVersion.toLatin1();

    SERVER_INFORMATION hostInfo;
    hostInfo.address = hostnameStr.data();
    hostInfo.serverInfoAppVersion = siAppVersion.data();

    // Older GFE versions didn't have this field
    QByteArray siGfeVersion;
    if (!m_Computer->gfeVersion.isNull()) {
        siGfeVersion = m_Computer->gfeVersion.toLatin1();
    }
    if (!siGfeVersion.isNull()) {
        hostInfo.serverInfoGfeVersion = siGfeVersion.data();
    }

    int err = LiStartConnection(&hostInfo, &m_StreamConfig, &k_ConnCallbacks,
                                &m_VideoCallbacks, &k_AudioCallbacks,
                                NULL, 0, NULL, 0);
    if (err != 0) {
        // We already displayed an error dialog in the stage failure
        // listener.
        s_ActiveSessionSemaphore.release();
        return;
    }

    // Pump the message loop to update the UI
    emit connectionStarted();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // Show the streaming window
    SDL_ShowWindow(m_Window);

    // Capture the mouse
    SDL_SetRelativeMouseMode(SDL_TRUE);

    // Disable the screen saver
    SDL_DisableScreenSaver();

    // Raise the priority of the main thread, since it handles
    // time-sensitive video rendering
    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to set main thread to high priority: %s",
                    SDL_GetError());
    }

    // Hijack this thread to be the SDL main thread. We have to do this
    // because we want to suspend all Qt processing until the stream is over.
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Quit event received");
            goto DispatchDeferredCleanup;
        case SDL_USEREVENT: {
            SDL_Event nextEvent;

            SDL_assert(event.user.code == SDL_CODE_FRAME_READY);

            // Drop any earlier frames
            while (SDL_PeepEvents(&nextEvent,
                                  1,
                                  SDL_GETEVENT,
                                  SDL_USEREVENT,
                                  SDL_USEREVENT) == 1) {
                m_VideoDecoder->dropFrame(&event.user);
                event = nextEvent;
            }

            // Render the last frame
            m_VideoDecoder->renderFrame(&event.user);
            break;
        }
        case SDL_KEYUP:
        case SDL_KEYDOWN:
            inputHandler.handleKeyEvent(&event.key);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            inputHandler.handleMouseButtonEvent(&event.button);
            break;
        case SDL_MOUSEMOTION:
            inputHandler.handleMouseMotionEvent(&event.motion);
            break;
        case SDL_MOUSEWHEEL:
            inputHandler.handleMouseWheelEvent(&event.wheel);
            break;
        case SDL_CONTROLLERAXISMOTION:
            inputHandler.handleControllerAxisEvent(&event.caxis);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            inputHandler.handleControllerButtonEvent(&event.cbutton);
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            inputHandler.handleControllerDeviceEvent(&event.cdevice);
            break;
        }
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SDL_WaitEvent() failed: %s",
                 SDL_GetError());

DispatchDeferredCleanup:
    // Uncapture the mouse and hide the window immediately,
    // so we can return to the Qt GUI ASAP.
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_EnableScreenSaver();
    if (m_Window != nullptr) {
        SDL_HideWindow(m_Window);
    }

    // Destroy the decoder, since this must be done on the main thread
    SDL_AtomicLock(&m_DecoderLock);
    delete m_VideoDecoder;
    m_VideoDecoder = nullptr;
    SDL_AtomicUnlock(&m_DecoderLock);

    // Cleanup can take a while, so dispatch it to a worker thread.
    // When it is complete, it will release our s_ActiveSessionSemaphore
    // reference.
    QThreadPool::globalInstance()->start(new DeferredSessionCleanupTask());
}

