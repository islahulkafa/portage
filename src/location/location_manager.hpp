#pragma once

#include "location/location_types.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace portage::location {

class LocationManager {
  public:
    static LocationManager &Instance();

    void Initialize();
    void Reset();

    void SetLocation(double lat, double lon, double alt = 10.0, float accuracy = 5.0f,
                     float speed = 0.0f, float bearing = 0.0f);
    LocationFix GetLastKnownLocation(const std::string &provider = "gps");

    bool IsProviderEnabled(const std::string &provider) const;
    void SetProviderEnabled(const std::string &provider, bool enabled);
    std::vector<std::string> GetAllProviders() const;

    void RequestLocationUpdates(const std::string &provider, int64_t min_time_ms,
                                float min_distance_m, ALocationListener_onLocationChanged callback,
                                void *context);
    void RemoveLocationUpdates(ALocationListener_onLocationChanged callback);

    bool AddCircularGeofence(int fence_id, double lat, double lon, float radius_m,
                             int64_t expiration_ms, int64_t dwell_duration_ms,
                             uint32_t transition_mask, AGeofenceListener_onTransition callback,
                             void *context);
    bool RemoveGeofence(int fence_id);

    void SetSimulatedVelocity(float speed_mps, float bearing_deg);
    void AdvanceSimulation(float delta_seconds);

    static double CalculateDistanceMeters(double lat1, double lon1, double lat2, double lon2);

  private:
    LocationManager();
    ~LocationManager();

    LocationManager(const LocationManager &) = delete;
    LocationManager &operator=(const LocationManager &) = delete;

    void StartWorkerThread();
    void StopWorkerThread();
    void WorkerLoop();
    void CheckGeofencesLocked(int64_t now_ms);
    void TryQueryWindowsLocationLocked();

    mutable std::mutex mutex_;
    LocationFix current_fix_;

    bool gps_enabled_{true};
    bool network_enabled_{true};
    bool passive_enabled_{true};

    float simulated_speed_{0.0f};
    float simulated_bearing_{0.0f};

    struct ListenerEntry {
        ALocationListener_onLocationChanged callback;
        void *context;
        int64_t min_time_ms;
        float min_distance_m;
        int64_t last_dispatch_time_ms;
        LocationFix last_dispatched_fix;
        std::string provider;
    };
    std::vector<ListenerEntry> listeners_;

    struct GeofenceEntry {
        GeofenceCircle circle;
        AGeofenceListener_onTransition callback;
        void *context;
    };
    std::unordered_map<int, GeofenceEntry> geofences_;

    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    std::condition_variable cv_;
    bool com_initialized_{false};
    bool has_windows_location_{false};
};

} // namespace portage::location
