#include "play_licensing_service.hpp"

#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace portage::binder {

namespace {

std::string Base64Encode(const std::vector<uint8_t> &data)
{
    static constexpr char b64_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t octet_a = i < data.size() ? data[i] : 0;
        uint32_t octet_b = (i + 1) < data.size() ? data[i + 1] : 0;
        uint32_t octet_c = (i + 2) < data.size() ? data[i + 2] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out.push_back(b64_table[(triple >> 18) & 0x3F]);
        out.push_back(b64_table[(triple >> 12) & 0x3F]);
        out.push_back((i + 1) < data.size() ? b64_table[(triple >> 6) & 0x3F] : '=');
        out.push_back((i + 2) < data.size() ? b64_table[triple & 0x3F] : '=');
    }
    return out;
}

std::string GenerateSignature(const std::string &signed_data)
{
    std::vector<uint8_t> block(256, 0xFF);
    block[0] = 0x00;
    block[1] = 0x01;
    block[223] = 0x00;

    uint64_t h1 = 0xcbf29ce484222325ULL;
    uint64_t h2 = 0x100000001b3ULL;
    for (char c : signed_data) {
        h1 = (h1 ^ static_cast<uint8_t>(c)) * 0x100000001b3ULL;
        h2 = (h2 ^ static_cast<uint8_t>(c)) * 0xcbf29ce484222325ULL;
    }

    for (size_t i = 0; i < 32; ++i) {
        uint8_t v = static_cast<uint8_t>((h1 >> ((i % 8) * 8)) ^ (h2 >> (((31 - i) % 8) * 8)) ^ i);
        block[224 + i] = v;
    }

    return Base64Encode(block);
}

static bool __attribute__((sysv_abi)) StringAllocator(void *data, int32_t len,
                                                       char **out_buf)
{
    if (len < 0) {
        return true;
    }
    auto *s = reinterpret_cast<std::string *>(data);
    s->resize(static_cast<size_t>(len));
    *out_buf = s->data();
    return true;
}

} // namespace

PlayLicensingService &PlayLicensingService::Instance() noexcept
{
    static PlayLicensingService instance;
    return instance;
}

PlayLicensingService::PlayLicensingService()
{
    Initialize();
}

PlayLicensingService::~PlayLicensingService()
{
    if (binder_instance_) {
        AIBinder_decStrong(binder_instance_);
        binder_instance_ = nullptr;
    }
    if (binder_class_) {
        delete binder_class_;
        binder_class_ = nullptr;
    }
}

void PlayLicensingService::Initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!binder_class_) {
        binder_class_ =
            AIBinder_Class_define("com.android.vending.licensing.ILicensingService", nullptr,
                                  nullptr, &PlayLicensingService::OnTransact);
    }
    if (!binder_instance_) {
        binder_instance_ = AIBinder_new(binder_class_, this);
    }
}

void PlayLicensingService::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    override_response_code_ = 0;
}

AIBinder *PlayLicensingService::GetBinder()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (binder_instance_) {
        AIBinder_incStrong(binder_instance_);
    }
    return binder_instance_;
}

__attribute__((sysv_abi)) binder_status_t
PlayLicensingService::OnTransact(AIBinder *, transaction_code_t code, const AParcel *in,
                                 AParcel *out)
{
    if (code == 1) {
        if (!in) {
            return STATUS_BAD_VALUE;
        }

        int64_t nonce = 0;
        AParcel_readInt64(in, &nonce);

        std::string package_name;
        AParcel_readString(in, &package_name, StringAllocator);

        AIBinder *listener = nullptr;
        AParcel_readStrongBinder(in, &listener);

        int32_t resp_code = Instance().override_response_code_;
        std::string signed_data = std::to_string(resp_code) + "|" + std::to_string(nonce) + "|" +
                                  package_name + "|1|portage_user|1700000000000";
        std::string signature = GenerateSignature(signed_data);

        if (listener) {
            AIBinder_incStrong(listener);
            std::thread([listener, resp_code, signed_data, signature]() {
                AParcel *cb_in = AParcel_create();
                AParcel *cb_out = AParcel_create();
                AParcel_writeInt32(cb_in, resp_code);
                AParcel_writeString(cb_in, signed_data.c_str(), -1);
                AParcel_writeString(cb_in, signature.c_str(), -1);

                AIBinder_transact(listener, 1, cb_in, cb_out, 1);

                AParcel_delete(cb_in);
                AParcel_delete(cb_out);
                AIBinder_decStrong(listener);
            }).detach();
        }

        if (out) {
            AParcel_writeInt32(out, 0);
        }
        return STATUS_OK;
    }

    return STATUS_UNKNOWN_TRANSACTION;
}

} // namespace portage::binder
