#include "location/location_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <locationapi.h>
#include <objbase.h>

#include <chrono>
#include <cmath>

namespace portage::location {

namespace {
constexpr double EARTH_RADIUS_METERS = 6371000.0;
constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;
} // namespace

LocationManager &LocationManager::Instance()
{
    static LocationManager instance;
    return instance;
}

LocationManager::LocationManager() { Initialize(); }

LocationManager::~LocationManager()
{
    StopWorkerThread();
    if (com_initialized_) {
        CoUninitialize();
    }
}

void LocationManager::Initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);

    current_fix_.provider = "gps";
    current_fix_.latitude = 37.4220;
    current_fix_.longitude = -122.0841;
    current_fix_.altitude = 10.0;
    current_fix_.accuracy = 5.0f;
    current_fix_.speed = 0.0f;
    current_fix_.bearing = 0.0f;

    auto now_sys = std::chrono::system_clock::now();
    current_fix_.time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now_sys.time_since_epoch()).count();
    current_fix_.elapsed_realtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count();

    gps_enabled_ = true;
    network_enabled_ = true;
    passive_enabled_ = true;
    simulated_speed_ = 0.0f;
    simulated_bearing_ = 0.0f;

    if (!com_initialized_) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            com_initialized_ = true;
        }
    }
}

void LocationManager::Reset()
{
    StopWorkerThread();

    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.clear();
    geofences_.clear();
    simulated_speed_ = 0.0f;
    simulated_bearing_ = 0.0f;

    current_fix_.provider = "gps";
    current_fix_.latitude = 37.4220;
    current_fix_.longitude = -122.0841;
    current_fix_.altitude = 10.0;
    current_fix_.accuracy = 5.0f;
    current_fix_.speed = 0.0f;
    current_fix_.bearing = 0.0f;
}

void LocationManager::SetLocation(double lat, double lon, double alt, float accuracy, float speed,
                                  float bearing)
{
    std::unique_lock<std::mutex> lock(mutex_);
    current_fix_.latitude = lat;
    current_fix_.longitude = lon;
    current_fix_.altitude = alt;
    current_fix_.accuracy = accuracy;
    current_fix_.speed = speed;
    current_fix_.bearing = bearing;

    auto now_sys = std::chrono::system_clock::now();
    current_fix_.time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now_sys.time_since_epoch()).count();
    current_fix_.elapsed_realtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count();

    CheckGeofencesLocked(current_fix_.time_ms);
}

LocationFix LocationManager::GetLastKnownLocation(const std::string &provider)
{
    std::lock_guard<std::mutex> lock(mutex_);
    LocationFix fix = current_fix_;
    fix.provider = provider.empty() ? "gps" : provider;
    return fix;
}

bool LocationManager::IsProviderEnabled(const std::string &provider) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (provider == "gps") {
        return gps_enabled_;
    }
    if (provider == "network") {
        return network_enabled_;
    }
    if (provider == "passive") {
        return passive_enabled_;
    }
    return false;
}

void LocationManager::SetProviderEnabled(const std::string &provider, bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (provider == "gps") {
        gps_enabled_ = enabled;
    } else if (provider == "network") {
        network_enabled_ = enabled;
    } else if (provider == "passive") {
        passive_enabled_ = enabled;
    }
}

std::vector<std::string> LocationManager::GetAllProviders() const
{
    return {"gps", "network", "passive"};
}

void LocationManager::RequestLocationUpdates(const std::string &provider, int64_t min_time_ms,
                                             float min_distance_m,
                                             ALocationListener_onLocationChanged callback,
                                             void *context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ListenerEntry entry{
        .callback = callback,
        .context = context,
        .min_time_ms = min_time_ms > 0 ? min_time_ms : 0,
        .min_distance_m = min_distance_m > 0.0f ? min_distance_m : 0.0f,
        .last_dispatch_time_ms = 0,
        .last_dispatched_fix = current_fix_,
        .provider = provider.empty() ? "gps" : provider,
    };
    listeners_.push_back(entry);

    StartWorkerThread();
}

void LocationManager::RemoveLocationUpdates(ALocationListener_onLocationChanged callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = listeners_.begin(); it != listeners_.end();) {
        if (it->callback == callback) {
            it = listeners_.erase(it);
        } else {
            ++it;
        }
    }

    if (listeners_.empty()) {
        StopWorkerThread();
    }
}

bool LocationManager::AddCircularGeofence(int fence_id, double lat, double lon, float radius_m,
                                          int64_t expiration_ms, int64_t dwell_duration_ms,
                                          uint32_t transition_mask,
                                          AGeofenceListener_onTransition callback, void *context)
{
    std::lock_guard<std::mutex> lock(mutex_);
    GeofenceCircle circle{
        .id = fence_id,
        .latitude = lat,
        .longitude = lon,
        .radius_meters = radius_m,
        .expiration_ms = expiration_ms,
        .dwell_duration_ms = dwell_duration_ms,
        .transition_mask = transition_mask,
        .last_state = 0,
        .inside_since_ms = 0,
        .dwell_triggered = false,
    };
    geofences_[fence_id] = GeofenceEntry{circle, callback, context};

    auto now_sys = std::chrono::system_clock::now();
    int64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now_sys.time_since_epoch()).count();
    CheckGeofencesLocked(now_ms);
    return true;
}

bool LocationManager::RemoveGeofence(int fence_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return geofences_.erase(fence_id) > 0;
}

void LocationManager::SetSimulatedVelocity(float speed_mps, float bearing_deg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    simulated_speed_ = speed_mps;
    simulated_bearing_ = bearing_deg;
    current_fix_.speed = speed_mps;
    current_fix_.bearing = bearing_deg;

    if (simulated_speed_ > 0.0f) {
        StartWorkerThread();
    }
}

void LocationManager::AdvanceSimulation(float delta_seconds)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (delta_seconds <= 0.0f) {
        return;
    }

    if (simulated_speed_ > 0.0f) {
        double dist = static_cast<double>(simulated_speed_ * delta_seconds);
        double bearing_rad = static_cast<double>(simulated_bearing_) * DEG_TO_RAD;
        double lat_rad = current_fix_.latitude * DEG_TO_RAD;

        double dlat = (dist * std::cos(bearing_rad)) / EARTH_RADIUS_METERS;
        double dlon = (dist * std::sin(bearing_rad)) / (EARTH_RADIUS_METERS * std::cos(lat_rad));

        current_fix_.latitude += dlat * RAD_TO_DEG;
        current_fix_.longitude += dlon * RAD_TO_DEG;
        current_fix_.speed = simulated_speed_;
        current_fix_.bearing = simulated_bearing_;
    }

    auto now_sys = std::chrono::system_clock::now();
    int64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now_sys.time_since_epoch()).count();
    current_fix_.time_ms = now_ms;
    current_fix_.elapsed_realtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count();

    CheckGeofencesLocked(now_ms);

    std::vector<std::pair<ALocationListener_onLocationChanged, void *>> dispatches;
    for (auto &listener : listeners_) {
        if (now_ms - listener.last_dispatch_time_ms >= listener.min_time_ms) {
            double dist = CalculateDistanceMeters(current_fix_.latitude, current_fix_.longitude,
                                                  listener.last_dispatched_fix.latitude,
                                                  listener.last_dispatched_fix.longitude);
            if (dist >= static_cast<double>(listener.min_distance_m) ||
                listener.last_dispatch_time_ms == 0) {
                listener.last_dispatch_time_ms = now_ms;
                listener.last_dispatched_fix = current_fix_;
                dispatches.push_back({listener.callback, listener.context});
            }
        }
    }

    LocationFix fix_copy = current_fix_;
    lock.unlock();

    for (const auto &[cb, ctx] : dispatches) {
        if (cb) {
            cb(&fix_copy, ctx);
        }
    }
}

double LocationManager::CalculateDistanceMeters(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = (lat2 - lat1) * DEG_TO_RAD;
    double dlon = (lon2 - lon1) * DEG_TO_RAD;
    double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
               std::cos(lat1 * DEG_TO_RAD) * std::cos(lat2 * DEG_TO_RAD) * std::sin(dlon / 2.0) *
                   std::sin(dlon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return EARTH_RADIUS_METERS * c;
}

void LocationManager::StartWorkerThread()
{
    if (running_.exchange(true)) {
        return;
    }
    worker_thread_ = std::thread(&LocationManager::WorkerLoop, this);
}

void LocationManager::StopWorkerThread()
{
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void LocationManager::WorkerLoop()
{
    auto last_time = std::chrono::steady_clock::now();
    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(50), [this] { return !running_.load(); });
        if (!running_) {
            break;
        }

        auto now_steady = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now_steady - last_time).count();
        last_time = now_steady;

        if (simulated_speed_ > 0.0f) {
            double dist = static_cast<double>(simulated_speed_ * dt);
            double bearing_rad = static_cast<double>(simulated_bearing_) * DEG_TO_RAD;
            double lat_rad = current_fix_.latitude * DEG_TO_RAD;

            double dlat = (dist * std::cos(bearing_rad)) / EARTH_RADIUS_METERS;
            double dlon =
                (dist * std::sin(bearing_rad)) / (EARTH_RADIUS_METERS * std::cos(lat_rad));

            current_fix_.latitude += dlat * RAD_TO_DEG;
            current_fix_.longitude += dlon * RAD_TO_DEG;
            current_fix_.speed = simulated_speed_;
            current_fix_.bearing = simulated_bearing_;
        }

        auto now_sys = std::chrono::system_clock::now();
        int64_t now_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now_sys.time_since_epoch())
                .count();
        current_fix_.time_ms = now_ms;
        current_fix_.elapsed_realtime_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now_steady.time_since_epoch())
                .count();

        CheckGeofencesLocked(now_ms);

        std::vector<std::pair<ALocationListener_onLocationChanged, void *>> dispatches;
        for (auto &listener : listeners_) {
            if (now_ms - listener.last_dispatch_time_ms >= listener.min_time_ms) {
                double dist = CalculateDistanceMeters(current_fix_.latitude, current_fix_.longitude,
                                                      listener.last_dispatched_fix.latitude,
                                                      listener.last_dispatched_fix.longitude);
                if (dist >= static_cast<double>(listener.min_distance_m) ||
                    listener.last_dispatch_time_ms == 0) {
                    listener.last_dispatch_time_ms = now_ms;
                    listener.last_dispatched_fix = current_fix_;
                    dispatches.push_back({listener.callback, listener.context});
                }
            }
        }

        LocationFix fix_copy = current_fix_;
        lock.unlock();

        for (const auto &[cb, ctx] : dispatches) {
            if (cb) {
                cb(&fix_copy, ctx);
            }
        }
    }
}

void LocationManager::CheckGeofencesLocked(int64_t now_ms)
{
    std::vector<std::tuple<AGeofenceListener_onTransition, int, int, void *>> transitions;

    for (auto &[id, entry] : geofences_) {
        if (entry.circle.expiration_ms > 0 && now_ms > entry.circle.expiration_ms) {
            continue;
        }

        double dist = CalculateDistanceMeters(current_fix_.latitude, current_fix_.longitude,
                                              entry.circle.latitude, entry.circle.longitude);
        bool inside = (dist <= static_cast<double>(entry.circle.radius_meters));

        if (entry.circle.last_state == 0) {
            entry.circle.last_state = inside ? 1 : 2;
            if (inside) {
                entry.circle.inside_since_ms = now_ms;
                if (entry.circle.transition_mask & GEOFENCE_TRANSITION_ENTER) {
                    transitions.push_back(
                        {entry.callback, id, GEOFENCE_TRANSITION_ENTER, entry.context});
                }
            }
        } else if (inside && entry.circle.last_state == 2) {
            entry.circle.last_state = 1;
            entry.circle.inside_since_ms = now_ms;
            entry.circle.dwell_triggered = false;
            if (entry.circle.transition_mask & GEOFENCE_TRANSITION_ENTER) {
                transitions.push_back(
                    {entry.callback, id, GEOFENCE_TRANSITION_ENTER, entry.context});
            }
        } else if (!inside && entry.circle.last_state == 1) {
            entry.circle.last_state = 2;
            entry.circle.dwell_triggered = false;
            if (entry.circle.transition_mask & GEOFENCE_TRANSITION_EXIT) {
                transitions.push_back(
                    {entry.callback, id, GEOFENCE_TRANSITION_EXIT, entry.context});
            }
        } else if (inside && entry.circle.last_state == 1 && !entry.circle.dwell_triggered &&
                   (entry.circle.transition_mask & GEOFENCE_TRANSITION_DWELL)) {
            if (entry.circle.dwell_duration_ms > 0 &&
                now_ms - entry.circle.inside_since_ms >= entry.circle.dwell_duration_ms) {
                entry.circle.dwell_triggered = true;
                transitions.push_back(
                    {entry.callback, id, GEOFENCE_TRANSITION_DWELL, entry.context});
            }
        }
    }

    LocationFix fix_copy = current_fix_;
    for (const auto &[cb, fence_id, trans, ctx] : transitions) {
        if (cb) {
            cb(fence_id, trans, &fix_copy, ctx);
        }
    }
}

void LocationManager::TryQueryWindowsLocationLocked()
{
    ILocation *location = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Location, nullptr, CLSCTX_INPROC_SERVER, IID_ILocation,
                                  reinterpret_cast<void **>(&location));
    if (SUCCEEDED(hr) && location) {
        ILocationReport *report = nullptr;
        hr = location->GetReport(IID_ILatLongReport, &report);
        if (SUCCEEDED(hr) && report) {
            ILatLongReport *lat_long = nullptr;
            hr = report->QueryInterface(IID_ILatLongReport, reinterpret_cast<void **>(&lat_long));
            if (SUCCEEDED(hr) && lat_long) {
                DOUBLE lat = 0.0;
                DOUBLE lon = 0.0;
                DOUBLE alt = 0.0;
                DOUBLE err = 0.0;
                if (SUCCEEDED(lat_long->GetLatitude(&lat)) &&
                    SUCCEEDED(lat_long->GetLongitude(&lon))) {
                    current_fix_.latitude = lat;
                    current_fix_.longitude = lon;
                    has_windows_location_ = true;
                    if (SUCCEEDED(lat_long->GetAltitude(&alt))) {
                        current_fix_.altitude = alt;
                        current_fix_.has_altitude = true;
                    }
                    if (SUCCEEDED(lat_long->GetErrorRadius(&err))) {
                        current_fix_.accuracy = static_cast<float>(err);
                        current_fix_.has_accuracy = true;
                    }
                }
                lat_long->Release();
            }
            report->Release();
        }
        location->Release();
    }
}

} // namespace portage::location
