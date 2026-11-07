#include "property_manager.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace portage::property {

PropertyManager &PropertyManager::Instance() noexcept
{
    static PropertyManager instance;
    return instance;
}

PropertyManager::PropertyManager() = default;

PropertyManager::~PropertyManager() { Reset(); }

void PropertyManager::Reset() noexcept
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (prop_area_) {
        UnmapViewOfFile(prop_area_);
        prop_area_ = nullptr;
    }
    if (map_handle_) {
        CloseHandle(map_handle_);
        map_handle_ = NULL;
    }
    if (serial_area_) {
        UnmapViewOfFile(serial_area_);
        serial_area_ = nullptr;
    }
    if (serial_map_handle_) {
        CloseHandle(serial_map_handle_);
        serial_map_handle_ = NULL;
    }
    properties_.clear();
    area_views_.clear();
    serial_views_.clear();
    global_serial_ = 1;
    initialized_ = false;
}

bool PropertyManager::Initialize(const std::filesystem::path &rootfs_path) noexcept
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }

    map_handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     static_cast<DWORD>(PROP_AREA_SIZE), nullptr);
    if (!map_handle_) {
        return false;
    }

    prop_area_ = static_cast<uint8_t *>(
        MapViewOfFile(map_handle_, FILE_MAP_ALL_ACCESS, 0, 0, PROP_AREA_SIZE));
    if (!prop_area_) {
        CloseHandle(map_handle_);
        map_handle_ = NULL;
        return false;
    }

    serial_map_handle_ =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, nullptr);
    if (!serial_map_handle_) {
        UnmapViewOfFile(prop_area_);
        prop_area_ = nullptr;
        CloseHandle(map_handle_);
        map_handle_ = NULL;
        return false;
    }

    serial_area_ =
        static_cast<uint8_t *>(MapViewOfFile(serial_map_handle_, FILE_MAP_ALL_ACCESS, 0, 0, 4096));
    if (!serial_area_) {
        CloseHandle(serial_map_handle_);
        serial_map_handle_ = NULL;
        UnmapViewOfFile(prop_area_);
        prop_area_ = nullptr;
        CloseHandle(map_handle_);
        map_handle_ = NULL;
        return false;
    }

    std::memset(prop_area_, 0, PROP_AREA_SIZE);
    std::memset(serial_area_, 0, 4096);

    auto *header = reinterpret_cast<PropAreaHeader *>(prop_area_);
    header->magic = PROP_AREA_MAGIC;
    header->version = PROP_AREA_VERSION;
    header->serial = 0;
    header->bytes_used = sizeof(PropTrieNode);

    auto *root = reinterpret_cast<PropTrieNode *>(prop_area_ + sizeof(PropAreaHeader));
    std::memset(root, 0, sizeof(PropTrieNode));

    auto *serial_header = reinterpret_cast<PropAreaHeader *>(serial_area_);
    serial_header->magic = PROP_AREA_MAGIC;
    serial_header->version = PROP_AREA_VERSION;
    serial_header->serial = global_serial_;
    serial_header->bytes_used = 0;

    initialized_ = true;

    PopulateDefaultProperties();

    if (!rootfs_path.empty()) {
        LoadBuildPropFile(rootfs_path / "system" / "build.prop");
        LoadBuildPropFile(rootfs_path / "default.prop");
    }

    LoadEnvironmentOverrides();
    return true;
}

void PropertyManager::PopulateDefaultProperties()
{
    SetProperty("ro.build.version.sdk", "34");
    SetProperty("ro.build.version.release", "14");
    SetProperty("ro.build.version.security_patch", "2024-08-05");
    SetProperty("ro.product.cpu.abi", "x86_64");
    SetProperty("ro.product.cpu.abilist", "x86_64");
    SetProperty("ro.product.model", "Portage Virtual Android on Windows 11");
    SetProperty("ro.product.brand", "Portage");
    SetProperty("ro.product.name", "portage_x86_64");
    SetProperty("ro.product.device", "portage_x86_64");
    SetProperty("ro.product.manufacturer", "Portage");
    SetProperty("ro.debuggable", "1");
    SetProperty("ro.secure", "0");
    SetProperty("persist.sys.timezone", "UTC");
    SetProperty("persist.sys.locale", "en-US");
    SetProperty("sys.boot_completed", "1");
    SetProperty("init.svc.bootanim", "stopped");
}

void PropertyManager::LoadBuildPropFile(const std::filesystem::path &prop_file)
{
    if (!std::filesystem::exists(prop_file)) {
        return;
    }

    std::ifstream file(prop_file);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') {
            continue;
        }

        size_t eq_pos = line.find('=', start);
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(start, eq_pos - start);
        std::string val = line.substr(eq_pos + 1);

        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
            key.pop_back();
        }
        size_t val_start = val.find_first_not_of(" \t");
        if (val_start != std::string::npos) {
            val = val.substr(val_start);
        } else {
            val.clear();
        }

        if (!key.empty()) {
            SetProperty(key, val);
        }
    }
}

void PropertyManager::LoadEnvironmentOverrides()
{
    LPWCH env_strings = GetEnvironmentStringsW();
    if (!env_strings) {
        return;
    }

    for (LPWCH var = env_strings; *var; var += wcslen(var) + 1) {
        std::wstring wline(var);
        if (wline.starts_with(L"PORTAGE_PROP_")) {
            size_t eq = wline.find(L'=');
            if (eq != std::wstring::npos) {
                std::wstring wkey = wline.substr(13, eq - 13);
                std::wstring wval = wline.substr(eq + 1);

                std::string key;
                for (wchar_t c : wkey) {
                    if (c == L'_') {
                        key.push_back('.');
                    } else {
                        key.push_back(static_cast<char>(std::tolower(c)));
                    }
                }
                std::string val(wval.begin(), wval.end());
                SetProperty(key, val);
            }
        }
    }
    FreeEnvironmentStringsW(env_strings);
}

uint32_t PropertyManager::AllocateObj(size_t size)
{
    if (!prop_area_) {
        return 0;
    }
    auto *header = reinterpret_cast<PropAreaHeader *>(prop_area_);
    uint32_t aligned = static_cast<uint32_t>((size + 3) & ~3);
    if (header->bytes_used + aligned > PROP_AREA_SIZE - sizeof(PropAreaHeader)) {
        return 0;
    }
    uint32_t off = header->bytes_used;
    header->bytes_used += aligned;
    return off;
}

PropTrieNode *PropertyManager::ToNode(uint32_t off)
{
    if (!prop_area_) {
        return nullptr;
    }
    auto *header = reinterpret_cast<PropAreaHeader *>(prop_area_);
    if (off + sizeof(PropTrieNode) > header->bytes_used) {
        return nullptr;
    }
    return reinterpret_cast<PropTrieNode *>(prop_area_ + sizeof(PropAreaHeader) + off);
}

PropInfo *PropertyManager::ToPropInfo(uint32_t off)
{
    if (!prop_area_) {
        return nullptr;
    }
    auto *header = reinterpret_cast<PropAreaHeader *>(prop_area_);
    if (off + sizeof(PropInfo) > header->bytes_used) {
        return nullptr;
    }
    return reinterpret_cast<PropInfo *>(prop_area_ + sizeof(PropAreaHeader) + off);
}

PropTrieNode *PropertyManager::FindOrCreateChildNode(PropTrieNode *parent, std::string_view segment)
{
    if (!parent) {
        return nullptr;
    }

    if (parent->children == 0) {
        size_t node_size = sizeof(PropTrieNode) + segment.size() + 1;
        uint32_t off = AllocateObj(node_size);
        if (off == 0) {
            return nullptr;
        }
        auto *child = ToNode(off);
        child->namelen = static_cast<uint8_t>(segment.size());
        std::memcpy(child->name, segment.data(), segment.size());
        child->name[segment.size()] = '\0';
        parent->children = off;
        return child;
    }

    uint32_t curr_off = parent->children;
    while (curr_off != 0) {
        auto *curr = ToNode(curr_off);
        if (!curr) {
            return nullptr;
        }

        std::string_view curr_name(curr->name, curr->namelen);
        int cmp = segment.compare(curr_name);
        if (cmp == 0) {
            return curr;
        } else if (cmp < 0) {
            if (curr->left == 0) {
                size_t node_size = sizeof(PropTrieNode) + segment.size() + 1;
                uint32_t off = AllocateObj(node_size);
                if (off == 0) {
                    return nullptr;
                }
                auto *child = ToNode(off);
                child->namelen = static_cast<uint8_t>(segment.size());
                std::memcpy(child->name, segment.data(), segment.size());
                child->name[segment.size()] = '\0';
                curr->left = off;
                return child;
            }
            curr_off = curr->left;
        } else {
            if (curr->right == 0) {
                size_t node_size = sizeof(PropTrieNode) + segment.size() + 1;
                uint32_t off = AllocateObj(node_size);
                if (off == 0) {
                    return nullptr;
                }
                auto *child = ToNode(off);
                child->namelen = static_cast<uint8_t>(segment.size());
                std::memcpy(child->name, segment.data(), segment.size());
                child->name[segment.size()] = '\0';
                curr->right = off;
                return child;
            }
            curr_off = curr->right;
        }
    }

    return nullptr;
}

PropInfo *PropertyManager::FindPropInfo(std::string_view name)
{
    if (!prop_area_) {
        return nullptr;
    }

    auto *curr = reinterpret_cast<PropTrieNode *>(prop_area_ + sizeof(PropAreaHeader));
    size_t start = 0;
    while (start < name.size()) {
        size_t dot = name.find('.', start);
        std::string_view segment =
            (dot == std::string_view::npos) ? name.substr(start) : name.substr(start, dot - start);
        start = (dot == std::string_view::npos) ? name.size() : dot + 1;

        if (curr->children == 0) {
            return nullptr;
        }

        uint32_t child_off = curr->children;
        bool found = false;
        while (child_off != 0) {
            auto *child = ToNode(child_off);
            if (!child) {
                return nullptr;
            }
            std::string_view curr_name(child->name, child->namelen);
            int cmp = segment.compare(curr_name);
            if (cmp == 0) {
                curr = child;
                found = true;
                break;
            } else if (cmp < 0) {
                child_off = child->left;
            } else {
                child_off = child->right;
            }
        }

        if (!found) {
            return nullptr;
        }
    }

    if (curr->prop == 0) {
        return nullptr;
    }
    return ToPropInfo(curr->prop);
}

PropInfo *PropertyManager::CreateOrUpdatePropInfo(std::string_view name, std::string_view value)
{
    if (!prop_area_) {
        return nullptr;
    }

    auto *curr = reinterpret_cast<PropTrieNode *>(prop_area_ + sizeof(PropAreaHeader));
    size_t start = 0;
    while (start < name.size()) {
        size_t dot = name.find('.', start);
        std::string_view segment =
            (dot == std::string_view::npos) ? name.substr(start) : name.substr(start, dot - start);
        start = (dot == std::string_view::npos) ? name.size() : dot + 1;

        curr = FindOrCreateChildNode(curr, segment);
        if (!curr) {
            return nullptr;
        }
    }

    size_t val_len = std::min(value.size(), PROP_VALUE_MAX - 1);
    PropInfo *pi = nullptr;

    if (curr->prop == 0) {
        size_t info_size = sizeof(PropInfo) + name.size() + 1;
        uint32_t prop_off = AllocateObj(info_size);
        if (prop_off == 0) {
            return nullptr;
        }
        pi = ToPropInfo(prop_off);
        pi->serial = (static_cast<uint32_t>(val_len) << 24) | ((global_serial_ << 1) & 0x00FFFFFF);
        std::memcpy(pi->value, value.data(), val_len);
        pi->value[val_len] = '\0';
        std::memcpy(pi->name, name.data(), std::min(name.size(), PROP_NAME_MAX - 1));
        pi->name[std::min(name.size(), PROP_NAME_MAX - 1)] = '\0';
        curr->prop = prop_off;
    } else {
        pi = ToPropInfo(curr->prop);
        pi->serial |= 1; // set dirty bit
        std::memcpy(pi->value, value.data(), val_len);
        pi->value[val_len] = '\0';
        pi->serial =
            (static_cast<uint32_t>(val_len) << 24) | (((global_serial_ + 1) << 1) & 0x00FFFFFF);
    }

    global_serial_++;
    if (serial_area_) {
        auto *serial_header = reinterpret_cast<PropAreaHeader *>(serial_area_);
        serial_header->serial = global_serial_;
    }

    SyncViews();

    return pi;
}

void PropertyManager::RegisterPropertyArea(uint8_t *p)
{
    if (!p)
        return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    area_views_.push_back(p);
}

void PropertyManager::RegisterSerialArea(uint8_t *p)
{
    if (!p)
        return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    serial_views_.push_back(p);
}

void PropertyManager::SyncViews()
{
    if (prop_area_) {
        auto *header = reinterpret_cast<PropAreaHeader *>(prop_area_);
        size_t total_used = sizeof(PropAreaHeader) + header->bytes_used;
        for (auto *view : area_views_) {
            DWORD old_prot = 0;
            VirtualProtect(view, total_used, PAGE_READWRITE, &old_prot);
            std::memcpy(view, prop_area_, total_used);
            VirtualProtect(view, total_used, old_prot, &old_prot);
        }
    }
    if (serial_area_) {
        for (auto *sview : serial_views_) {
            DWORD old_prot = 0;
            VirtualProtect(sview, sizeof(PropAreaHeader), PAGE_READWRITE, &old_prot);
            auto *shdr = reinterpret_cast<PropAreaHeader *>(sview);
            shdr->serial = global_serial_;
            VirtualProtect(sview, sizeof(PropAreaHeader), old_prot, &old_prot);
        }
    }
}

std::string PropertyManager::GetProperty(std::string_view name, std::string_view default_val) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = properties_.find(std::string(name));
    if (it != properties_.end()) {
        return it->second;
    }
    return std::string(default_val);
}

bool PropertyManager::SetProperty(std::string_view name, std::string_view value)
{
    if (name.empty() || name.size() >= PROP_NAME_MAX) {
        return false;
    }
    if (value.size() >= PROP_VALUE_MAX) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!initialized_) {
        return false;
    }

    PropInfo *pi = CreateOrUpdatePropInfo(name, value);
    if (!pi) {
        return false;
    }

    properties_[std::string(name)] = std::string(value);
    return true;
}

std::vector<std::pair<std::string, std::string>> PropertyManager::GetAllProperties() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(properties_.size());
    for (const auto &[k, v] : properties_) {
        result.emplace_back(k, v);
    }
    return result;
}

uint32_t PropertyManager::HandleSetPropMessage(std::span<const uint8_t> payload)
{
    if (payload.size() < sizeof(uint32_t)) {
        return PROP_ERROR_READ_CMD;
    }

    if (payload.size() < sizeof(PropMsgSetProp)) {
        return PROP_ERROR_READ_DATA;
    }

    const auto *msg = reinterpret_cast<const PropMsgSetProp *>(payload.data());
    if (msg->cmd != PROP_MSG_SETPROP && msg->cmd != PROP_MSG_SETPROP2) {
        return PROP_ERROR_READ_CMD;
    }

    size_t name_len = strnlen(msg->name, PROP_NAME_MAX);
    if (name_len == 0 || name_len >= PROP_NAME_MAX) {
        return PROP_ERROR_INVALID_NAME;
    }

    size_t val_len = strnlen(msg->value, PROP_VALUE_MAX);
    if (val_len >= PROP_VALUE_MAX) {
        return PROP_ERROR_INVALID_VALUE;
    }

    std::string_view name(msg->name, name_len);
    std::string_view value(msg->value, val_len);

    if (!SetProperty(name, value)) {
        return PROP_ERROR_SET_FAILED;
    }

    return PROP_SUCCESS;
}

} // namespace portage::property
