#pragma once

#include "binder_ndk.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace portage::binder {

class PlayLicensingService {
  public:
    static PlayLicensingService &Instance() noexcept;

    AIBinder *GetBinder();
    void Reset();

    void SetLicenseResponseCode(int32_t code) noexcept { override_response_code_ = code; }

    static __attribute__((sysv_abi)) binder_status_t
    OnTransact(AIBinder *binder, transaction_code_t code, const AParcel *in, AParcel *out);

  private:
    PlayLicensingService();
    ~PlayLicensingService();

    void Initialize();

    mutable std::mutex mutex_{};
    AIBinder_Class *binder_class_{nullptr};
    AIBinder *binder_instance_{nullptr};
    int32_t override_response_code_{0};
};

} // namespace portage::binder
