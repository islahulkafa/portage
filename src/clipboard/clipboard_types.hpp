#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace portage::clipboard {

inline constexpr const char *MIMETYPE_TEXT_PLAIN = "text/plain";
inline constexpr const char *MIMETYPE_TEXT_HTML = "text/html";
inline constexpr const char *MIMETYPE_TEXT_URILIST = "text/uri-list";
inline constexpr const char *MIMETYPE_IMAGE_PNG = "image/png";
inline constexpr const char *MIMETYPE_IMAGE_JPEG = "image/jpeg";

enum DragAction : uint32_t {
    ACTION_DRAG_STARTED = 1,
    ACTION_DRAG_LOCATION = 2,
    ACTION_DROP = 3,
    ACTION_DRAG_ENDED = 4,
    ACTION_DRAG_ENTERED = 5,
    ACTION_DRAG_EXITED = 6
};

struct ClipItem {
    std::string text;
    std::string html_text;
    std::string uri;
    std::vector<uint8_t> binary_data;
    std::string mime_type{MIMETYPE_TEXT_PLAIN};
};

struct ClipData {
    std::string label;
    std::vector<std::string> mime_types;
    std::vector<ClipItem> items;
};

typedef __attribute__((sysv_abi)) void (*AClipboard_OnPrimaryClipChangedListener)(void *context);
typedef __attribute__((sysv_abi)) void (*ADragDrop_OnDropListener)(float x, float y,
                                                                   const char *uri, void *context);

} // namespace portage::clipboard
