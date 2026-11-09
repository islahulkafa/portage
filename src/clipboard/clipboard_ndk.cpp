#include "clipboard/clipboard_ndk.hpp"
#include "clipboard/clipboard_manager.hpp"

#include <cstring>

using namespace portage::clipboard;

struct AClipboard {
    int dummy = 0;
};

static AClipboard g_default_clipboard;

extern "C" {

__attribute__((sysv_abi)) AClipboard *AClipboard_create()
{
    return &g_default_clipboard;
}

__attribute__((sysv_abi)) void AClipboard_destroy(AClipboard *)
{
}

__attribute__((sysv_abi)) int AClipboard_hasText(AClipboard *)
{
    std::string text = ClipboardManager::Instance().GetText();
    return text.empty() ? 0 : 1;
}

__attribute__((sysv_abi)) int AClipboard_getText(AClipboard *, char *buffer, size_t max_len)
{
    if (!buffer || max_len == 0) {
        return -1;
    }
    std::string text = ClipboardManager::Instance().GetText();
    size_t copy_len = (text.size() < max_len - 1) ? text.size() : (max_len - 1);
    std::memcpy(buffer, text.c_str(), copy_len);
    buffer[copy_len] = '\0';
    return static_cast<int>(copy_len);
}

__attribute__((sysv_abi)) int AClipboard_setText(AClipboard *, const char *text)
{
    if (!text) {
        return -1;
    }
    bool ok = ClipboardManager::Instance().SetText(text);
    return ok ? 0 : -1;
}

__attribute__((sysv_abi)) int AClipboard_hasPrimaryClip(AClipboard *)
{
    return ClipboardManager::Instance().HasPrimaryClip() ? 1 : 0;
}

__attribute__((sysv_abi)) void AClipboard_clear(AClipboard *)
{
    ClipboardManager::Instance().ClearPrimaryClip();
}

__attribute__((sysv_abi)) int AClipboard_addPrimaryClipChangedListener(
    AClipboard *, AClipboard_OnPrimaryClipChangedListener callback, void *context)
{
    if (!callback) {
        return -1;
    }
    ClipboardManager::Instance().AddPrimaryClipChangedListener(callback, context);
    return 0;
}

__attribute__((sysv_abi)) int AClipboard_removePrimaryClipChangedListener(
    AClipboard *, AClipboard_OnPrimaryClipChangedListener callback)
{
    if (!callback) {
        return -1;
    }
    ClipboardManager::Instance().RemovePrimaryClipChangedListener(callback);
    return 0;
}

__attribute__((sysv_abi)) int ADragDrop_registerDropListener(ADragDrop_OnDropListener callback,
                                                            void *context)
{
    if (!callback) {
        return -1;
    }
    ClipboardManager::Instance().AddDropListener(callback, context);
    return 0;
}

__attribute__((sysv_abi)) int ADragDrop_unregisterDropListener(ADragDrop_OnDropListener callback)
{
    if (!callback) {
        return -1;
    }
    ClipboardManager::Instance().RemoveDropListener(callback);
    return 0;
}

} // extern "C"
