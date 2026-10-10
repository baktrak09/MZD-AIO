// Touchscreen-only Android Auto session hooks.
// Preserves Mazda's original session creation and destruction.

#define LOG_TAG "TOUCH_LIFECYCLE"
#include "log.h"
#include "common/preload.h"
#include "touch/touch.h"

#include <dlfcn.h>
#include <pthread.h>

namespace {

typedef int (*CreateFn)(const char *, void *, void *, void **);
typedef int (*DestroyFn)(void *);

CreateFn real_create = nullptr;
DestroyFn real_destroy = nullptr;

void *libaap_handle = nullptr;

pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;
bool touch_running = false;
void *touch_session = nullptr;

} // namespace

extern "C" PRELOAD_EXPORT
int aap_create_session(const char *cfg, void *r1,
                       void *callbacks, void **out_handle)
{
    if (!real_create) {
        real_create = reinterpret_cast<CreateFn>(
            resolve_real_symbol(
                "aap_create_session",
                "libaap_interface.so",
                "/usr/lib/libaap_interface.so",
                reinterpret_cast<void *>(&aap_create_session),
                &libaap_handle));
    }

    if (!real_create) {
        LOGC("Cannot resolve original aap_create_session");
        return 0x103;
    }

    // Always let Mazda create the original AA session.
    int rc = real_create(cfg, r1, callbacks, out_handle);

    if (rc != 0 || !out_handle || !*out_handle)
        return rc;

    // Only activate inside the factory AA process.
    void *blm = dlopen("/jci/aapa/blmjciaapa.so",
                       RTLD_NOW | RTLD_NOLOAD);

    if (!blm)
        return rc;

    dlclose(blm);

    pthread_mutex_lock(&session_mutex);

    if (!touch_running) {
        touch_running = touch_post_aap_create_session();
        if (touch_running) {
            touch_session = *out_handle;
            LOGD("Touchscreen reader started for session %p",
                 touch_session);
        } else {
            touch_session = nullptr;
            LOGE("Touchscreen reader failed to start");
        }
    }

    pthread_mutex_unlock(&session_mutex);

    return rc;
}

extern "C" PRELOAD_EXPORT
int aap_destroy_session(void *handle)
{
    if (!real_destroy) {
        real_destroy = reinterpret_cast<DestroyFn>(
            resolve_real_symbol(
                "aap_destroy_session",
                "libaap_interface.so",
                "/usr/lib/libaap_interface.so",
                reinterpret_cast<void *>(&aap_destroy_session),
                &libaap_handle));
    }

    pthread_mutex_lock(&session_mutex);

    if (touch_running && handle == touch_session) {
        touch_pre_aap_destroy_session();
        touch_running = false;
        touch_session = nullptr;
        LOGD("Touchscreen reader stopped");
    }

    pthread_mutex_unlock(&session_mutex);

    if (!real_destroy) {
        LOGC("Cannot resolve original aap_destroy_session");
        return 0x103;
    }

    return real_destroy(handle);
}
