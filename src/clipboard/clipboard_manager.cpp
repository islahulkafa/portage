#include "clipboard/clipboard_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace portage::clipboard {

namespace {

std::wstring Utf8ToWide(const std::string &str)
{
    if (str.empty()) {
        return L"";
    }
    int size_needed =
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0],
                        size_needed);
    return wstr;
}

std::string WideToUtf8(const std::wstring &wstr)
{
    if (wstr.empty()) {
        return "";
    }
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                                          static_cast<int>(wstr.size()), nullptr, 0, nullptr,
                                          nullptr);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &str[0],
                        size_needed, nullptr, nullptr);
    return str;
}

bool SafeOpenClipboard(HWND hwnd = nullptr)
{
    for (int i = 0; i < 5; ++i) {
        if (OpenClipboard(hwnd)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

} // namespace

ClipboardManager &ClipboardManager::Instance()
{
    static ClipboardManager instance;
    return instance;
}

ClipboardManager::ClipboardManager()
{
    Reset();
}

void ClipboardManager::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    primary_clip_ = ClipData{};
    has_clip_ = false;
    clip_listeners_.clear();
    drop_listeners_.clear();
    updating_from_host_ = false;
}

void ClipboardManager::SetInteractiveSync(bool enable)
{
    std::lock_guard<std::mutex> lock(mutex_);
    interactive_sync_ = enable;
}

bool ClipboardManager::SetPrimaryClip(const ClipData &clip)
{
    std::lock_guard<std::mutex> lock(mutex_);
    primary_clip_ = clip;
    has_clip_ = !clip.items.empty();

    if (!updating_from_host_) {
        SyncToHostClipboardLocked(clip);
    }
    NotifyClipChangedLocked();
    return true;
}

ClipData ClipboardManager::GetPrimaryClip()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_clip_ && interactive_sync_) {
        SyncFromHostClipboardLocked();
    }
    return primary_clip_;
}

bool ClipboardManager::HasPrimaryClip() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return has_clip_;
}

void ClipboardManager::ClearPrimaryClip()
{
    std::lock_guard<std::mutex> lock(mutex_);
    primary_clip_ = ClipData{};
    has_clip_ = false;

    if (interactive_sync_) {
        if (SafeOpenClipboard(nullptr)) {
            EmptyClipboard();
            CloseClipboard();
        }
    }
    NotifyClipChangedLocked();
}

bool ClipboardManager::SetText(const std::string &text, const std::string &label)
{
    ClipData clip;
    clip.label = label.empty() ? "text" : label;
    clip.mime_types = {MIMETYPE_TEXT_PLAIN};
    ClipItem item;
    item.text = text;
    item.mime_type = MIMETYPE_TEXT_PLAIN;
    clip.items.push_back(item);
    return SetPrimaryClip(clip);
}

std::string ClipboardManager::GetText()
{
    ClipData clip = GetPrimaryClip();
    if (!clip.items.empty()) {
        return clip.items[0].text;
    }
    return "";
}

bool ClipboardManager::SetHtml(const std::string &html, const std::string &plain_text,
                               const std::string &label)
{
    ClipData clip;
    clip.label = label.empty() ? "html" : label;
    clip.mime_types = {MIMETYPE_TEXT_HTML, MIMETYPE_TEXT_PLAIN};
    ClipItem item;
    item.html_text = html;
    item.text = plain_text.empty() ? html : plain_text;
    item.mime_type = MIMETYPE_TEXT_HTML;
    clip.items.push_back(item);
    return SetPrimaryClip(clip);
}

std::string ClipboardManager::GetHtml()
{
    ClipData clip = GetPrimaryClip();
    if (!clip.items.empty()) {
        return clip.items[0].html_text;
    }
    return "";
}

bool ClipboardManager::SetUri(const std::string &uri, const std::string &label)
{
    ClipData clip;
    clip.label = label.empty() ? "uri" : label;
    clip.mime_types = {MIMETYPE_TEXT_URILIST};
    ClipItem item;
    item.uri = uri;
    item.text = uri;
    item.mime_type = MIMETYPE_TEXT_URILIST;
    clip.items.push_back(item);
    return SetPrimaryClip(clip);
}

std::string ClipboardManager::GetUri()
{
    ClipData clip = GetPrimaryClip();
    if (!clip.items.empty()) {
        return clip.items[0].uri;
    }
    return "";
}

bool ClipboardManager::SetImage(const std::vector<uint8_t> &data, const std::string &mime,
                                const std::string &label)
{
    ClipData clip;
    clip.label = label.empty() ? "image" : label;
    clip.mime_types = {mime};
    ClipItem item;
    item.binary_data = data;
    item.mime_type = mime;
    clip.items.push_back(item);
    return SetPrimaryClip(clip);
}

void ClipboardManager::AddPrimaryClipChangedListener(
    AClipboard_OnPrimaryClipChangedListener callback, void *context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    clip_listeners_.push_back({callback, context});
}

void ClipboardManager::RemovePrimaryClipChangedListener(
    AClipboard_OnPrimaryClipChangedListener callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = clip_listeners_.begin(); it != clip_listeners_.end();) {
        if (it->first == callback) {
            it = clip_listeners_.erase(it);
        } else {
            ++it;
        }
    }
}

void ClipboardManager::OnHostClipboardUpdated()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (updating_from_host_) {
        return;
    }
    updating_from_host_ = true;
    bool changed = SyncFromHostClipboardLocked();
    updating_from_host_ = false;
    if (changed) {
        NotifyClipChangedLocked();
    }
}

void ClipboardManager::AddDropListener(ADragDrop_OnDropListener callback, void *context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    drop_listeners_.push_back({callback, context});
}

void ClipboardManager::RemoveDropListener(ADragDrop_OnDropListener callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = drop_listeners_.begin(); it != drop_listeners_.end();) {
        if (it->first == callback) {
            it = drop_listeners_.erase(it);
        } else {
            ++it;
        }
    }
}

void ClipboardManager::DispatchDropEvent(float x, float y, const std::string &uri)
{
    std::vector<std::pair<ADragDrop_OnDropListener, void *>> listeners;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners = drop_listeners_;
    }
    for (const auto &[cb, ctx] : listeners) {
        if (cb) {
            cb(x, y, uri.c_str(), ctx);
        }
    }
}

void ClipboardManager::NotifyClipChangedLocked()
{
    auto listeners = clip_listeners_;
    for (const auto &[cb, ctx] : listeners) {
        if (cb) {
            cb(ctx);
        }
    }
}

bool ClipboardManager::SyncToHostClipboardLocked(const ClipData &clip)
{
    if (!interactive_sync_) {
        return true;
    }

    std::string text_to_set;
    for (const auto &item : clip.items) {
        if (!item.text.empty()) {
            text_to_set = item.text;
            break;
        }
        if (!item.uri.empty()) {
            text_to_set = item.uri;
            break;
        }
    }

    if (text_to_set.empty()) {
        return true;
    }

    if (!SafeOpenClipboard(nullptr)) {
        return false;
    }

    EmptyClipboard();

    std::wstring wstr = Utf8ToWide(text_to_set);
    size_t bytes = (wstr.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void *p = GlobalLock(hMem);
        if (p) {
            std::memcpy(p, wstr.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }

    CloseClipboard();
    return true;
}

bool ClipboardManager::SyncFromHostClipboardLocked()
{
    if (!interactive_sync_) {
        return false;
    }

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) && !IsClipboardFormatAvailable(CF_HDROP)) {
        return false;
    }

    if (!SafeOpenClipboard(nullptr)) {
        return false;
    }

    bool changed = false;

    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            const wchar_t *pWStr = static_cast<const wchar_t *>(GlobalLock(hData));
            if (pWStr) {
                std::string utf8_text = WideToUtf8(pWStr);
                GlobalUnlock(hData);

                if (primary_clip_.items.empty() || primary_clip_.items[0].text != utf8_text) {
                    primary_clip_.label = "HostClipboard";
                    primary_clip_.mime_types = {MIMETYPE_TEXT_PLAIN};
                    primary_clip_.items.clear();
                    ClipItem item;
                    item.text = utf8_text;
                    item.mime_type = MIMETYPE_TEXT_PLAIN;
                    primary_clip_.items.push_back(item);
                    has_clip_ = true;
                    changed = true;
                }
            }
        }
    } else if (IsClipboardFormatAvailable(CF_HDROP)) {
        HANDLE hData = GetClipboardData(CF_HDROP);
        if (hData) {
            HDROP hDrop = static_cast<HDROP>(GlobalLock(hData));
            if (hDrop) {
                UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
                if (count > 0) {
                    primary_clip_.label = "HostFiles";
                    primary_clip_.mime_types = {MIMETYPE_TEXT_URILIST};
                    primary_clip_.items.clear();
                    for (UINT i = 0; i < count; ++i) {
                        wchar_t buf[MAX_PATH];
                        if (DragQueryFileW(hDrop, i, buf, MAX_PATH) > 0) {
                            ClipItem item;
                            item.uri = "file:///" + WideToUtf8(buf);
                            item.mime_type = MIMETYPE_TEXT_URILIST;
                            primary_clip_.items.push_back(item);
                        }
                    }
                    has_clip_ = !primary_clip_.items.empty();
                    changed = true;
                }
                GlobalUnlock(hData);
            }
        }
    }

    CloseClipboard();
    return changed;
}

} // namespace portage::clipboard
