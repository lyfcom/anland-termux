#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/surface_control.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#include "anw_hidden.h"
#include "direct_surface.h"

struct direct_slot {
    struct direct_surface *surface;
    AHardwareBuffer *buffer;
    int release_fence;
    bool available;
};

typedef ASurfaceControl *(*from_java_fn)(JNIEnv *, jobject);
typedef const native_handle_t *(*get_native_handle_fn)(const AHardwareBuffer *);
typedef void (*buffer_release_fn)(void *, int);
typedef void (*set_buffer_fn)(ASurfaceTransaction *, ASurfaceControl *, AHardwareBuffer *,
                              int, void *, buffer_release_fn);

struct direct_surface {
    ASurfaceControl *parent;
    ASurfaceControl *layer;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    atomic_uint refs;
    bool stopped;
    bool pending_commit;
    unsigned int next_slot;
    struct direct_slot slots[DIRECT_BUFFER_COUNT];
};

static get_native_handle_fn get_native_handle;
static void (*acquire_control)(ASurfaceControl *);
using set_on_commit_fn = void (*)(ASurfaceTransaction *, void *, void (*)(void *, ASurfaceTransactionStats *));
static set_on_commit_fn set_on_commit;
static set_buffer_fn set_buffer_with_release;

static void unref(struct direct_surface *surface)
{
    if (atomic_fetch_sub(&surface->refs, 1) != 1)
        return;

    for (int i = 0; i < DIRECT_BUFFER_COUNT; i++) {
        struct direct_slot *slot = &surface->slots[i];
        if (slot->release_fence >= 0)
            close(slot->release_fence);
        if (slot->buffer)
            AHardwareBuffer_release(slot->buffer);
    }
    ASurfaceControl_release(surface->layer);
    ASurfaceControl_release(surface->parent);
    pthread_cond_destroy(&surface->changed);
    pthread_mutex_destroy(&surface->lock);
    free(surface);
}

static struct direct_surface *create_layer(ASurfaceControl *parent);

struct direct_surface *direct_surface_create(JNIEnv *env, jobject parent)
{
    if (!parent)
        return NULL;

    from_java_fn from_java = (from_java_fn)dlsym(RTLD_DEFAULT, "ASurfaceControl_fromJava");
    get_native_handle = (get_native_handle_fn)dlsym(RTLD_DEFAULT, "AHardwareBuffer_getNativeHandle");
    set_buffer_with_release = (set_buffer_fn)dlsym(RTLD_DEFAULT, "ASurfaceTransaction_setBufferWithRelease");
    set_on_commit = (set_on_commit_fn)dlsym(RTLD_DEFAULT, "ASurfaceTransaction_setOnCommit");
    acquire_control = (void (*)(ASurfaceControl *))dlsym(RTLD_DEFAULT, "ASurfaceControl_acquire");
    if (!from_java || !get_native_handle || !set_buffer_with_release || !set_on_commit || !acquire_control)
        return NULL;

    ASurfaceControl *control = from_java(env, parent);
    if (!control)
        return nullptr;
    auto *surface = create_layer(control);
    ASurfaceControl_release(control);
    return surface;
}

struct direct_surface *direct_surface_recreate(struct direct_surface *previous)
{
    return create_layer(previous->parent);
}

static struct direct_surface *create_layer(ASurfaceControl *parent)
{
    ASurfaceControl *layer = ASurfaceControl_create(parent, "AnlandDirectPresentation");
    if (!layer)
        return nullptr;

    auto *surface = static_cast<struct direct_surface *>(calloc(1, sizeof(struct direct_surface)));
    if (!surface) {
        ASurfaceControl_release(layer);
        return nullptr;
    }
    acquire_control(parent);
    surface->parent = parent;
    surface->layer = layer;

    pthread_mutex_init(&surface->lock, NULL);
    pthread_cond_init(&surface->changed, NULL);
    atomic_init(&surface->refs, 1);
    for (int i = 0; i < DIRECT_BUFFER_COUNT; i++) {
        surface->slots[i].surface = surface;
        surface->slots[i].release_fence = -1;
        surface->slots[i].available = true;
    }
    return surface;
}

int direct_surface_export(struct direct_surface *surface, int width, int height,
                          int view_width, int view_height, int rotation,
                          int fds[DIRECT_BUFFER_COUNT], struct buf_info infos[DIRECT_BUFFER_COUNT])
{
    AHardwareBuffer_Desc desc{};
    desc.width = rotation ? height : width;
    desc.height = rotation ? width : height;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    // Request a CPU-writable allocation for the producer's linear DMA-BUF import.
    desc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER
               | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
    for (int i = 0; i < DIRECT_BUFFER_COUNT; i++) {
        struct direct_slot *slot = &surface->slots[i];
        if (!slot->buffer && AHardwareBuffer_allocate(&desc, &slot->buffer) != 0)
            return -1;

        AHardwareBuffer_Desc allocation;
        AHardwareBuffer_describe(slot->buffer, &allocation);
        const native_handle_t *handle = get_native_handle(slot->buffer);
        if (!handle || handle->numFds < 1 || allocation.width != desc.width
            || allocation.height != desc.height)
            return -1;

        fds[i] = dup(handle->data[0]);
        if (fds[i] < 0)
            return -1;

        infos[i] = {};
        infos[i].width = allocation.width;
        infos[i].height = allocation.height;
        infos[i].stride = allocation.stride * 4;
        infos[i].format = allocation.format | (static_cast<uint32_t>(rotation) << BUF_ROTATION_SHIFT);
    }

    ASurfaceTransaction *transaction = ASurfaceTransaction_create();
    ARect source = {0, 0, static_cast<int32_t>(desc.width), static_cast<int32_t>(desc.height)};
    ARect destination = {0, 0, view_width, view_height};
    const int transform = rotation == 1 ? ANATIVEWINDOW_TRANSFORM_ROTATE_270
        : rotation == 3 ? ANATIVEWINDOW_TRANSFORM_ROTATE_90 : ANATIVEWINDOW_TRANSFORM_IDENTITY;
    ASurfaceTransaction_setGeometry(transaction, surface->layer, source, destination, transform);
    ASurfaceTransaction_setBufferTransparency(transaction, surface->layer, ASURFACE_TRANSACTION_TRANSPARENCY_OPAQUE);
    ASurfaceTransaction_setVisibility(transaction, surface->layer, ASURFACE_TRANSACTION_VISIBILITY_SHOW);
    ASurfaceTransaction_apply(transaction);
    ASurfaceTransaction_delete(transaction);
    return 0;
}

int direct_surface_acquire(struct direct_surface *surface)
{
    pthread_mutex_lock(&surface->lock);
    for (;;) {
        if (surface->stopped) {
            pthread_mutex_unlock(&surface->lock);
            return -1;
        }
        for (unsigned int i = 0; !surface->pending_commit && i < DIRECT_BUFFER_COUNT; i++) {
            unsigned int index = (surface->next_slot + i) % DIRECT_BUFFER_COUNT;
            struct direct_slot *slot = &surface->slots[index];
            if (!slot->available)
                continue;

            slot->available = false;
            surface->next_slot = (index + 1) % DIRECT_BUFFER_COUNT;
            int fence = slot->release_fence;
            slot->release_fence = -1;
            pthread_mutex_unlock(&surface->lock);

            if (fence >= 0) {
                struct pollfd wait = {fence, POLLIN, 0};
                int result;
                do {
                    result = poll(&wait, 1, 1000);
                } while (result < 0 && errno == EINTR);
                close(fence);
                if (result <= 0 || !(wait.revents & POLLIN))
                    return -1;
            }
            return (int)index;
        }
        pthread_cond_wait(&surface->changed, &surface->lock);
    }
}

static void buffer_released(void *context, int fence)
{
    auto *slot = static_cast<struct direct_slot *>(context);
    struct direct_surface *surface = slot->surface;
    pthread_mutex_lock(&surface->lock);
    slot->release_fence = fence;
    slot->available = true;
    pthread_cond_signal(&surface->changed);
    pthread_mutex_unlock(&surface->lock);
    unref(surface);
}

static void frame_committed(void *context, ASurfaceTransactionStats *stats)
{
    (void)stats;
    auto *surface = static_cast<struct direct_surface *>(context);
    pthread_mutex_lock(&surface->lock);
    surface->pending_commit = false;
    pthread_cond_signal(&surface->changed);
    pthread_mutex_unlock(&surface->lock);
    unref(surface);
}

void direct_surface_cancel(struct direct_surface *surface, int index)
{
    pthread_mutex_lock(&surface->lock);
    surface->slots[index].available = true;
    pthread_mutex_unlock(&surface->lock);
}

void direct_surface_submit(struct direct_surface *surface, int index, int render_fence)
{
    struct direct_slot *slot = &surface->slots[index];
    pthread_mutex_lock(&surface->lock);
    surface->pending_commit = true;
    pthread_mutex_unlock(&surface->lock);

    // Keep at most one uncommitted update. Each callback also owns a reference
    // because SurfaceFlinger can deliver it after the renderer has stopped.
    atomic_fetch_add(&surface->refs, 2);
    ASurfaceTransaction *transaction = ASurfaceTransaction_create();
    set_buffer_with_release(transaction, surface->layer, slot->buffer, render_fence,
                            slot, buffer_released);
    set_on_commit(transaction, surface, frame_committed);
    ASurfaceTransaction_apply(transaction);
    ASurfaceTransaction_delete(transaction);
}

void direct_surface_stop(struct direct_surface *surface)
{
    pthread_mutex_lock(&surface->lock);
    surface->stopped = true;
    pthread_cond_signal(&surface->changed);
    pthread_mutex_unlock(&surface->lock);
}

void direct_surface_destroy(struct direct_surface *surface)
{
    ASurfaceTransaction *transaction = ASurfaceTransaction_create();
    ASurfaceTransaction_reparent(transaction, surface->layer, NULL);
    ASurfaceTransaction_apply(transaction);
    ASurfaceTransaction_delete(transaction);
    unref(surface);
}
