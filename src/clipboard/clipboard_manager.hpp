#pragma once

#include "clipboard/clipboard_types.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace portage::clipboard {

class ClipboardManager {
  public:
    static ClipboardManager &Instance();

    void Reset();
    void SetInteractiveSync(bool enable);

    bool SetPrimaryClip(const ClipData &clip);
    ClipData GetPrimaryClip();
    bool HasPrimaryClip() const;
    void ClearPrimaryClip();

    bool SetText(const std::string &text, const std::string &label = "");
    std::string GetText();

    bool SetHtml(const std::string &html, const std::string &plain_text = "",
                 const std::string &label = "");
    std::string GetHtml();

    bool SetUri(const std::string &uri, const std::string &label = "");
    std::string GetUri();

    bool SetImage(const std::vector<uint8_t> &data,
                  const std::string &mime = MIMETYPE_IMAGE_PNG,
                  const std::string &label = "");

    void AddPrimaryClipChangedListener(AClipboard_OnPrimaryClipChangedListener callback,
                                       void *context);
    void RemovePrimaryClipChangedListener(AClipboard_OnPrimaryClipChangedListener callback);
    void OnHostClipboardUpdated();

    void AddDropListener(ADragDrop_OnDropListener callback, void *context);
    void RemoveDropListener(ADragDrop_OnDropListener callback);
    void DispatchDropEvent(float x, float y, const std::string &uri);

  private:
    ClipboardManager();
    ~ClipboardManager() = default;

    ClipboardManager(const ClipboardManager &) = delete;
    ClipboardManager &operator=(const ClipboardManager &) = delete;

    void NotifyClipChangedLocked();
    bool SyncToHostClipboardLocked(const ClipData &clip);
    bool SyncFromHostClipboardLocked();

    mutable std::mutex mutex_;
    ClipData primary_clip_;
    bool has_clip_{false};
    bool interactive_sync_{true};
    bool updating_from_host_{false};

    std::vector<std::pair<AClipboard_OnPrimaryClipChangedListener, void *>> clip_listeners_;
    std::vector<std::pair<ADragDrop_OnDropListener, void *>> drop_listeners_;
};

} // namespace portage::clipboard
