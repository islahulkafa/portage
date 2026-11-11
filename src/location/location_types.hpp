#pragma once

#include <cstdint>
#include <string>

namespace portage::location {

enum class LocationProviderType { Gps, Network, Passive };

enum class LocationStatus {
    Success = 0,
    ErrorInvalidArg = -1,
    ErrorProviderDisabled = -2,
    ErrorNotFound = -3
};

enum GeofenceTransitionFlag : uint32_t {
    GEOFENCE_TRANSITION_ENTER = 1,
    GEOFENCE_TRANSITION_EXIT = 2,
    GEOFENCE_TRANSITION_DWELL = 4
};

struct ALocation {
    std::string provider = "gps";
    double latitude = 37.4220;
    double longitude = -122.0841;
    double altitude = 10.0;
    float accuracy = 5.0f;
    float speed = 0.0f;
    float bearing = 0.0f;
    int64_t time_ms = 0;
    int64_t elapsed_realtime_ns = 0;
    bool has_altitude = true;
    bool has_speed = true;
    bool has_bearing = true;
    bool has_accuracy = true;
};

using LocationFix = ALocation;

struct GeofenceCircle {
    int id = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    float radius_meters = 0.0f;
    int64_t expiration_ms = 0;
    int64_t dwell_duration_ms = 0;
    uint32_t transition_mask = GEOFENCE_TRANSITION_ENTER | GEOFENCE_TRANSITION_EXIT;
    int last_state = 0; // 0=unknown, 1=inside, 2=outside
    int64_t inside_since_ms = 0;
    bool dwell_triggered = false;
};

typedef
    __attribute__((sysv_abi)) void (*ALocationListener_onLocationChanged)(const ALocation *location,
                                                                          void *context);
typedef
    __attribute__((sysv_abi)) void (*ALocationListener_onStatusChanged)(const char *provider,
                                                                        int status, void *context);
typedef
    __attribute__((sysv_abi)) void (*AGeofenceListener_onTransition)(int fence_id, int transition,
                                                                     const ALocation *location,
                                                                     void *context);

} // namespace portage::location
