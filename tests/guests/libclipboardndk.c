#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct AClipboard;
typedef struct AClipboard AClipboard;

typedef void (*AClipboard_OnPrimaryClipChangedListener)(void *context);
typedef void (*ADragDrop_OnDropListener)(float x, float y, const char *uri, void *context);

__attribute__((visibility("default"))) AClipboard *AClipboard_create(void)
{
    return (AClipboard *)0;
}

__attribute__((visibility("default"))) void AClipboard_destroy(AClipboard *clip) { (void)clip; }

__attribute__((visibility("default"))) int AClipboard_hasText(AClipboard *clip)
{
    (void)clip;
    return 0;
}

__attribute__((visibility("default"))) int AClipboard_getText(AClipboard *clip, char *buffer,
                                                              size_t max_len)
{
    (void)clip;
    (void)buffer;
    (void)max_len;
    return 0;
}

__attribute__((visibility("default"))) int AClipboard_setText(AClipboard *clip, const char *text)
{
    (void)clip;
    (void)text;
    return 0;
}

__attribute__((visibility("default"))) int AClipboard_hasPrimaryClip(AClipboard *clip)
{
    (void)clip;
    return 0;
}

__attribute__((visibility("default"))) void AClipboard_clear(AClipboard *clip) { (void)clip; }

__attribute__((visibility("default"))) int AClipboard_addPrimaryClipChangedListener(
    AClipboard *clip, AClipboard_OnPrimaryClipChangedListener callback, void *context)
{
    (void)clip;
    (void)callback;
    (void)context;
    return 0;
}

__attribute__((visibility("default"))) int
AClipboard_removePrimaryClipChangedListener(AClipboard *clip,
                                            AClipboard_OnPrimaryClipChangedListener callback)
{
    (void)clip;
    (void)callback;
    return 0;
}

__attribute__((visibility("default"))) int
ADragDrop_registerDropListener(ADragDrop_OnDropListener callback, void *context)
{
    (void)callback;
    (void)context;
    return 0;
}

__attribute__((visibility("default"))) int
ADragDrop_unregisterDropListener(ADragDrop_OnDropListener callback)
{
    (void)callback;
    return 0;
}
