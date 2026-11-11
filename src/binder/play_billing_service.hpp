#pragma once

#include "binder_ndk.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace portage::binder {

struct SkuInfo {
    std::string sku{};
    std::string type{"inapp"};
    std::string price{"$4.99"};
    int64_t price_amount_micros{4990000};
    std::string currency{"USD"};
    std::string title{"Full Game Unlock"};
    std::string description{"Unlocks all game levels and content"};
};

struct PurchaseInfo {
    std::string order_id{"GPA.1234-5678-9012"};
    std::string package_name{"com.portage.gameengine"};
    std::string sku{"full_game_unlock"};
    int64_t purchase_time{1700000000000LL};
    int32_t purchase_state{0};
    std::string purchase_token{"token_full_game_unlock_123"};
    bool acknowledged{true};
};

class PlayBillingService {
  public:
    static PlayBillingService &Instance() noexcept;

    AIBinder *GetBinder();
    void Reset();

    void AddSku(const SkuInfo &sku);
    void AddPurchase(const PurchaseInfo &purchase);
    bool ConsumePurchase(const std::string &token);
    [[nodiscard]] bool HasPurchase(const std::string &sku) const;

    static __attribute__((sysv_abi)) binder_status_t OnTransact(AIBinder *binder,
                                                                transaction_code_t code,
                                                                const AParcel *in, AParcel *out);

  private:
    PlayBillingService();
    ~PlayBillingService();

    void Initialize();
    void PopulateDefaults();

    mutable std::mutex mutex_{};
    AIBinder_Class *binder_class_{nullptr};
    AIBinder *binder_instance_{nullptr};

    std::unordered_map<std::string, SkuInfo> skus_{};
    std::unordered_map<std::string, PurchaseInfo> purchases_{};
};

} // namespace portage::binder
