#include <stddef.h>
#include <stdint.h>

struct ANativeWindow {
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t ref_count;
};

static struct ANativeWindow g_default_window = {.width = 1280,
                                                .height = 720,
                                                .format = 1, // WINDOW_FORMAT_RGBA_8888
                                                .ref_count = 1};

__attribute__((visibility("default"))) void ANativeWindow_acquire(struct ANativeWindow *window)
{
    if (window) {
        window->ref_count++;
    }
}

__attribute__((visibility("default"))) void ANativeWindow_release(struct ANativeWindow *window)
{
    if (window && window->ref_count > 0) {
        window->ref_count--;
    }
}

__attribute__((visibility("default"))) int32_t ANativeWindow_getWidth(struct ANativeWindow *window)
{
    return window ? window->width : 0;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_getHeight(struct ANativeWindow *window)
{
    return window ? window->height : 0;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_getFormat(struct ANativeWindow *window)
{
    return window ? window->format : 0;
}

__attribute__((visibility("default"))) int32_t ANativeWindow_setBuffersGeometry(
    struct ANativeWindow *window, int32_t width, int32_t height, int32_t format)
{
    if (!window)
        return -1;
    if (width > 0)
        window->width = width;
    if (height > 0)
        window->height = height;
    if (format > 0)
        window->format = format;
    return 0;
}

__attribute__((visibility("default"))) struct ANativeWindow *ANativeWindow_create(int32_t width,
                                                                                  int32_t height)
{
    g_default_window.width = width > 0 ? width : 1280;
    g_default_window.height = height > 0 ? height : 720;
    g_default_window.format = 1;
    g_default_window.ref_count = 1;
    return &g_default_window;
}

struct AAsset;
struct AAssetManager {
    void *internal_ptr;
    struct AAsset *(*openFunc)(struct AAssetManager *mgr, const char *filename, int mode);
    int (*readFunc)(struct AAsset *asset, void *buf, size_t count);
    int64_t (*seekFunc)(struct AAsset *asset, int64_t offset, int whence);
    int64_t (*getLengthFunc)(struct AAsset *asset);
    const void *(*getBufferFunc)(struct AAsset *asset);
    void (*closeFunc)(struct AAsset *asset);
};

struct AAsset {
    void *internal_ptr;
    int (*readFunc)(struct AAsset *asset, void *buf, size_t count);
    int64_t (*seekFunc)(struct AAsset *asset, int64_t offset, int whence);
    int64_t (*getLengthFunc)(struct AAsset *asset);
    const void *(*getBufferFunc)(struct AAsset *asset);
    void (*closeFunc)(struct AAsset *asset);
};

__attribute__((visibility("default"))) struct AAsset *
AAssetManager_open(struct AAssetManager *mgr, const char *filename, int mode)
{
    if (!mgr || !mgr->openFunc)
        return NULL;
    return mgr->openFunc(mgr, filename, mode);
}

__attribute__((visibility("default"))) int AAsset_read(struct AAsset *asset, void *buf,
                                                       size_t count)
{
    if (!asset || !asset->readFunc)
        return -1;
    return asset->readFunc(asset, buf, count);
}

__attribute__((visibility("default"))) int64_t AAsset_seek(struct AAsset *asset, int64_t offset,
                                                           int whence)
{
    if (!asset || !asset->seekFunc)
        return -1;
    return asset->seekFunc(asset, offset, whence);
}

__attribute__((visibility("default"))) int64_t AAsset_getLength(struct AAsset *asset)
{
    if (!asset || !asset->getLengthFunc)
        return -1;
    return asset->getLengthFunc(asset);
}

__attribute__((visibility("default"))) const void *AAsset_getBuffer(struct AAsset *asset)
{
    if (!asset || !asset->getBufferFunc)
        return NULL;
    return asset->getBufferFunc(asset);
}

__attribute__((visibility("default"))) void AAsset_close(struct AAsset *asset)
{
    if (asset && asset->closeFunc) {
        asset->closeFunc(asset);
    }
}
