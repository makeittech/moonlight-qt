// This file contains hooks for several functions that allow
// Qt and SDL to more or less share DRM master ownership.
//
// This technique requires Linux v5.8 or later, or for Moonlight
// to run as root (with CAP_SYS_ADMIN). Prior to Linux v5.8,
// DRM_IOCTL_DROP_MASTER required CAP_SYS_ADMIN, which prevents
// our trick from working (without root, that is).
//
// The specific kernel change required to run without root is:
// https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=45bc3d26c95a8fc63a7d8668ca9e57ef0883351c

#define _GNU_SOURCE

#include <SDL.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

// We require SDL 2.0.15+ to hook because it supports sharing
// the DRM FD with our code. This avoids having multiple DRM FDs
// in flight at the same time which would significantly complicate
// the logic here because we'd need to figure out exactly which FD
// should be the master at any given time. With the position of our
// hooks, that is definitely not trivial.
#if SDL_VERSION_ATLEAST(2, 0, 15)

// Qt's DRM master FD grabbed by our hook
int g_QtDrmMasterFd = -1;
struct stat64 g_DrmMasterStat;

// The DRM master FD created for SDL
int g_SdlDrmMasterFd = -1;

// This hook will handle legacy DRM rendering
int drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId,
                   uint32_t x, uint32_t y, uint32_t *connectors, int count,
                   drmModeModeInfoPtr mode)
{
    // Grab the first DRM FD that makes it in here. This will be the Qt
    // EGLFS backend's DRM FD, which we will need later.
    if (g_QtDrmMasterFd == -1) {
        g_QtDrmMasterFd = fd;
        fstat64(g_QtDrmMasterFd, &g_DrmMasterStat);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Captured Qt EGLFS DRM master fd (legacy): %d",
                    g_QtDrmMasterFd);
    }

    // Call into the real thing
    return ((typeof(drmModeSetCrtc)*)dlsym(RTLD_NEXT, "drmModeSetCrtc"))(fd, crtcId, bufferId, x, y, connectors, count, mode);
}

// This hook will handle atomic DRM rendering
int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req,
                        uint32_t flags, void *user_data)
{
    // Grab the first DRM FD that makes it in here. This will be the Qt
    // EGLFS backend's DRM FD, which we will need later.
    if (g_QtDrmMasterFd == -1) {
        g_QtDrmMasterFd = fd;
        fstat64(g_QtDrmMasterFd, &g_DrmMasterStat);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Captured Qt EGLFS DRM master fd (atomic): %d",
                    g_QtDrmMasterFd);
    }

    // Call into the real thing
    return ((typeof(drmModeAtomicCommit)*)dlsym(RTLD_NEXT, "drmModeAtomicCommit"))(fd, req, flags, user_data);
}

// This hook will handle SDL's open() on the DRM device. We just need to
// hook this variant of open(), since that's what SDL uses. When we see
// the open a FD for the same card as the Qt DRM master FD, we'll drop
// master on the Qt FD to allow the new FD to have master.
int open(const char *pathname, int flags)
{
    // Call the real thing to do the open operation
    int fd = ((typeof(open)*)dlsym(RTLD_NEXT, "open"))(pathname, flags);

    // If the file was successfully opened and we have a DRM master FD,
    // check if the FD we just opened is a DRM device.
    if (fd >= 0 && g_QtDrmMasterFd != -1) {
        if (strncmp(pathname, "/dev/dri/card", 13) == 0) {
            // It's a DRM device, but is it _our_ DRM device?
            struct stat64 fdstat;

            fstat64(fd, &fdstat);
            if (g_DrmMasterStat.st_dev == fdstat.st_dev &&
                    g_DrmMasterStat.st_ino == fdstat.st_ino) {
                // It is our device. Time to do the magic!

                // This code assumes SDL only ever opens a single FD
                // for a given DRM device.
                SDL_assert(g_SdlDrmMasterFd == -1);

                // Drop master on Qt's FD so we can pick it up for SDL.
                if (drmDropMaster(g_QtDrmMasterFd) < 0) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "Failed to drop master on Qt DRM FD: %d",
                                 errno);
                    // Hope for the best
                    return fd;
                }

                // We are not allowed to call drmSetMaster() without CAP_SYS_ADMIN,
                // but since we just dropped the master, we can become master by
                // simply creating a new FD. Let's do it.
                close(fd);
                fd = ((typeof(open)*)dlsym(RTLD_NEXT, "open"))(pathname, flags);
                g_SdlDrmMasterFd = fd;
            }
        }
    }

    return fd;
}

// Our close() hook handles restoring DRM master to the Qt FD
// after SDL closes its DRM FD.
int close(int fd)
{
    // Call the real thing
    int ret = ((typeof(close)*)dlsym(RTLD_NEXT, "close"))(fd);
    if (ret == 0) {
        // If we just closed the SDL DRM master FD, restore master
        // to the Qt DRM FD. This works because the Qt DRM master FD
        // was master once before, so we can set it as master again
        // using drmSetMaster() without CAP_SYS_ADMIN.
        if (g_SdlDrmMasterFd != -1 && fd == g_SdlDrmMasterFd) {
            if (drmSetMaster(g_QtDrmMasterFd) < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Failed to restore master to Qt DRM FD: %d",
                             errno);
            }

            g_SdlDrmMasterFd = -1;
        }
    }
    return ret;
}

#endif
