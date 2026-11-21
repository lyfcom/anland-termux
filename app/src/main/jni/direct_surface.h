#ifndef ANLAND_DIRECT_SURFACE_H
#define ANLAND_DIRECT_SURFACE_H

#include <jni.h>
#include <stdbool.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { DIRECT_BUFFER_COUNT = 3 };

struct direct_surface;

struct direct_surface *direct_surface_create(JNIEnv *env, jobject parent);
int direct_surface_export(struct direct_surface *surface, int width, int height,
                          int view_width, int view_height,
                          int fds[DIRECT_BUFFER_COUNT], struct buf_info infos[DIRECT_BUFFER_COUNT]);
int direct_surface_acquire(struct direct_surface *surface);
void direct_surface_cancel(struct direct_surface *surface, int index);
void direct_surface_submit(struct direct_surface *surface, int index, int render_fence);
void direct_surface_stop(struct direct_surface *surface);
void direct_surface_destroy(struct direct_surface *surface);

#ifdef __cplusplus
}
#endif

#endif
