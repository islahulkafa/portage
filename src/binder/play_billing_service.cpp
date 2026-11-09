#include "play_billing_service.hpp"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace portage::binder {

namespace {

void WriteBundle(AParcel *out, const std::unordered_map<std::string, int32_t> &int_fields,
                 const std::unordered_map<std::string, std::string> &string_fields,
                 const std::unordered_map<std::string, std::vector<std::string>> &list_fields)
{
    AParcel *body = AParcel_create();
    int32_t count =
        static_cast<int32_t>(int_fields.size() + string_fields.size() + list_fields.size());

    for (const auto &[k, v] : int_fields) {
        AParcel_writeString(body, k.c_str(), -1);
        AParcel_writeInt32(body, 1);
        AParcel_writeInt32(body, v);
    }
    for (const auto &[k, v] : string_fields) {
        AParcel_writeString(body, k.c_str(), -1);
        AParcel_writeInt32(body, 0);
        AParcel_writeString(body, v.c_str(), -1);
    }
    for (const auto &[k, v] : list_fields) {
        AParcel_writeString(body, k.c_str(), -1);
        AParcel_writeInt32(body, 11);
        AParcel_writeInt32(body, static_cast<int32_t>(v.size()));
        for (const auto &item : v) {
            AParcel_writeInt32(body, 0);
            AParcel_writeString(body, item.c_str(), -1);
        }
    }

    int32_t body_size = AParcel_getDataSize(body);
    int32_t bundle_payload_len = body_size + 8;

    AParcel_writeInt32(out, 1);
    AParcel_writeInt32(out, bundle_payload_len);
    AParcel_writeInt32(out, 0x4C444E42);
    AParcel_writeInt32(out, count);

    if (body_size > 0 && out) {
        size_t cur = out->data_pos;
        if (cur + static_cast<size_t>(body_size) > out->buffer.size()) {
            out->buffer.resize(cur + static_cast<size_t>(body_size), 0);
        }
        std::memcpy(out->buffer.data() + cur, body->buffer.data(), static_cast<size_t>(body_size));
        out->data_pos += static_cast<size_t>(body_size);
    }

    AParcel_delete(body);
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

PlayBillingService &PlayBillingService::Instance() noexcept
{
    static PlayBillingService instance;
    return instance;
}

PlayBillingService::PlayBillingService()
{
    Initialize();
    PopulateDefaults();
}

PlayBillingService::~PlayBillingService()
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

void PlayBillingService::Initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!binder_class_) {
        binder_class_ =
            AIBinder_Class_define("com.android.vending.billing.IInAppBillingService", nullptr,
                                  nullptr, &PlayBillingService::OnTransact);
    }
    if (!binder_instance_) {
        binder_instance_ = AIBinder_new(binder_class_, this);
    }
}

void PlayBillingService::PopulateDefaults()
{
    skus_["full_game_unlock"] = SkuInfo{
        .sku = "full_game_unlock",
        .type = "inapp",
        .price = "$4.99",
        .price_amount_micros = 4990000,
        .currency = "USD",
        .title = "Full Game Unlock",
        .description = "Unlocks all game levels and content",
    };
    skus_["premium_pass"] = SkuInfo{
        .sku = "premium_pass",
        .type = "inapp",
        .price = "$1.99",
        .price_amount_micros = 1990000,
        .currency = "USD",
        .title = "Premium Season Pass",
        .description = "Access to all seasonal rewards",
    };

    purchases_["token_full_game_unlock_123"] = PurchaseInfo{
        .order_id = "GPA.1234-5678-9012",
        .package_name = "com.portage.gameengine",
        .sku = "full_game_unlock",
        .purchase_time = 1700000000000LL,
        .purchase_state = 0,
        .purchase_token = "token_full_game_unlock_123",
        .acknowledged = true,
    };
}

void PlayBillingService::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    skus_.clear();
    purchases_.clear();
    PopulateDefaults();
}

AIBinder *PlayBillingService::GetBinder()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (binder_instance_) {
        AIBinder_incStrong(binder_instance_);
    }
    return binder_instance_;
}

void PlayBillingService::AddSku(const SkuInfo &sku)
{
    std::lock_guard<std::mutex> lock(mutex_);
    skus_[sku.sku] = sku;
}

void PlayBillingService::AddPurchase(const PurchaseInfo &purchase)
{
    std::lock_guard<std::mutex> lock(mutex_);
    purchases_[purchase.purchase_token] = purchase;
}

bool PlayBillingService::ConsumePurchase(const std::string &token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = purchases_.find(token);
    if (it != purchases_.end()) {
        purchases_.erase(it);
        return true;
    }
    return false;
}

bool PlayBillingService::HasPurchase(const std::string &sku) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &[_, p] : purchases_) {
        if (p.sku == sku && p.purchase_state == 0) {
            return true;
        }
    }
    return false;
}

__attribute__((sysv_abi)) binder_status_t
PlayBillingService::OnTransact(AIBinder *, transaction_code_t code, const AParcel *in,
                               AParcel *out)
{
    auto &svc = Instance();
    std::lock_guard<std::mutex> lock(svc.mutex_);

    switch (code) {
    case 1: {
        int32_t api_ver = 3;
        if (in) {
            AParcel_readInt32(in, &api_ver);
        }
        if (out) {
            AParcel_writeInt32(out, 0);
            AParcel_writeInt32(out, 0);
        }
        return STATUS_OK;
    }

    case 2: {
        std::vector<std::string> details_list;
        for (const auto &[_, info] : svc.skus_) {
            std::string json = "{\"productId\":\"" + info.sku + "\"," +
                               "\"type\":\"" + info.type + "\"," +
                               "\"price\":\"" + info.price + "\"," +
                               "\"price_amount_micros\":" +
                               std::to_string(info.price_amount_micros) + "," +
                               "\"price_currency_code\":\"" + info.currency + "\"," +
                               "\"title\":\"" + info.title + "\"," +
                               "\"description\":\"" + info.description + "\"}";
            details_list.push_back(json);
        }

        if (out) {
            AParcel_writeInt32(out, 0);
            std::unordered_map<std::string, int32_t> int_fields;
            int_fields["RESPONSE_CODE"] = 0;
            std::unordered_map<std::string, std::string> str_fields;
            std::unordered_map<std::string, std::vector<std::string>> list_fields;
            list_fields["DETAILS_LIST"] = details_list;

            WriteBundle(out, int_fields, str_fields, list_fields);
        }
        return STATUS_OK;
    }

    case 3: {
        if (out) {
            AParcel_writeInt32(out, 0);
            std::unordered_map<std::string, int32_t> int_fields;
            int_fields["RESPONSE_CODE"] = 0;
            std::unordered_map<std::string, std::string> str_fields;
            str_fields["BUY_INTENT"] = "synthetic_buy_intent_ok";
            std::unordered_map<std::string, std::vector<std::string>> list_fields;

            WriteBundle(out, int_fields, str_fields, list_fields);
        }
        return STATUS_OK;
    }

    case 4: {
        std::vector<std::string> item_list;
        std::vector<std::string> data_list;
        std::vector<std::string> sig_list;

        for (const auto &[_, p] : svc.purchases_) {
            item_list.push_back(p.sku);
            std::string json = "{\"orderId\":\"" + p.order_id + "\"," +
                               "\"packageName\":\"" + p.package_name + "\"," +
                               "\"productId\":\"" + p.sku + "\"," +
                               "\"purchaseTime\":" + std::to_string(p.purchase_time) + "," +
                               "\"purchaseState\":" + std::to_string(p.purchase_state) + "," +
                               "\"purchaseToken\":\"" + p.purchase_token + "\"," +
                               "\"acknowledged\":" + (p.acknowledged ? "true" : "false") + "}";
            data_list.push_back(json);
            sig_list.push_back("MOCK_RECEIPT_SIGNATURE_OK");
        }

        if (out) {
            AParcel_writeInt32(out, 0);
            std::unordered_map<std::string, int32_t> int_fields;
            int_fields["RESPONSE_CODE"] = 0;
            std::unordered_map<std::string, std::string> str_fields;
            std::unordered_map<std::string, std::vector<std::string>> list_fields;
            list_fields["INAPP_PURCHASE_ITEM_LIST"] = item_list;
            list_fields["INAPP_PURCHASE_DATA_LIST"] = data_list;
            list_fields["INAPP_DATA_SIGNATURE_LIST"] = sig_list;

            WriteBundle(out, int_fields, str_fields, list_fields);
        }
        return STATUS_OK;
    }

    case 5: {
        std::string token;
        if (in) {
            int32_t api_ver = 0;
            AParcel_readInt32(in, &api_ver);
            std::string pkg;
            AParcel_readString(in, &pkg, StringAllocator);
            AParcel_readString(in, &token, StringAllocator);
        }

        if (!token.empty()) {
            svc.purchases_.erase(token);
        }

        if (out) {
            AParcel_writeInt32(out, 0);
            AParcel_writeInt32(out, 0);
        }
        return STATUS_OK;
    }

    default:
        return STATUS_UNKNOWN_TRANSACTION;
    }
}

} // namespace portage::binder
