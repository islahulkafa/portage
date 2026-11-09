#pragma once

#include "clipboard/clipboard_types.hpp"

#include <cstddef>
#include <cstdint>

struct AClipboard;

extern "C" {

__attribute__((sysv_abi)) AClipboard *AClipboard_create();
__attribute__((sysv_abi)) void AClipboard_destroy(AClipboard *clip);

__attribute__((sysv_abi)) int AClipboard_hasText(AClipboard *clip);
__attribute__((sysv_abi)) int AClipboard_getText(AClipboard *clip, char *buffer, size_t max_len);
__attribute__((sysv_abi)) int AClipboard_setText(AClipboard *clip, const char *text);

__attribute__((sysv_abi)) int AClipboard_hasPrimaryClip(AClipboard *clip);
__attribute__((sysv_abi)) void AClipboard_clear(AClipboard *clip);

__attribute__((sysv_abi)) int AClipboard_addPrimaryClipChangedListener(
    AClipboard *clip, portage::clipboard::AClipboard_OnPrimaryClipChangedListener callback,
    void *context);
__attribute__((sysv_abi)) int AClipboard_removePrimaryClipChangedListener(
    AClipboard *clip, portage::clipboard::AClipboard_OnPrimaryClipChangedListener callback);

__attribute__((sysv_abi)) int ADragDrop_registerDropListener(
    portage::clipboard::ADragDrop_OnDropListener callback, void *context);
__attribute__((sysv_abi)) int ADragDrop_unregisterDropListener(
    portage::clipboard::ADragDrop_OnDropListener callback);

} // extern "C"
