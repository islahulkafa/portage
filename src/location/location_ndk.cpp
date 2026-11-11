#include "location/location_ndk.hpp"
#include "location/location_manager.hpp"

using namespace portage::location;

struct ALocationManager {
    int dummy = 0;
};

static ALocationManager g_default_location_manager;

extern "C" {

__attribute__((sysv_abi)) ALocationManager *ALocationManager_create()
{
    return &g_default_location_manager;
}

__attribute__((sysv_abi)) void ALocationManager_destroy(ALocationManager *) {}

__attribute__((sysv_abi)) int ALocationManager_isProviderEnabled(ALocationManager *,
                                                                 const char *provider)
{
    if (!provider) {
        return 0;
    }
    return LocationManager::Instance().IsProviderEnabled(provider) ? 1 : 0;
}

__attribute__((sysv_abi)) ALocation *ALocationManager_getLastKnownLocation(ALocationManager *,
                                                                           const char *provider)
{
    std::string prov = provider ? provider : "gps";
    LocationFix fix = LocationManager::Instance().GetLastKnownLocation(prov);
    return new ALocation(fix);
}

__attribute__((sysv_abi)) int
ALocationManager_requestLocationUpdates(ALocationManager *, const char *provider,
                                        int64_t min_time_ms, float min_distance_m,
                                        ALocationListener_onLocationChanged callback, void *context)
{
    if (!callback) {
        return -1;
    }
    std::string prov = provider ? provider : "gps";
    LocationManager::Instance().RequestLocationUpdates(prov, min_time_ms, min_distance_m, callback,
                                                       context);
    return 0;
}

__attribute__((sysv_abi)) int
ALocationManager_removeUpdates(ALocationManager *, ALocationListener_onLocationChanged callback)
{
    if (!callback) {
        return -1;
    }
    LocationManager::Instance().RemoveLocationUpdates(callback);
    return 0;
}

__attribute__((sysv_abi)) double ALocation_getLatitude(const ALocation *loc)
{
    return loc ? loc->latitude : 0.0;
}

__attribute__((sysv_abi)) double ALocation_getLongitude(const ALocation *loc)
{
    return loc ? loc->longitude : 0.0;
}

__attribute__((sysv_abi)) double ALocation_getAltitude(const ALocation *loc)
{
    return loc ? loc->altitude : 0.0;
}

__attribute__((sysv_abi)) float ALocation_getAccuracy(const ALocation *loc)
{
    return loc ? loc->accuracy : 0.0f;
}

__attribute__((sysv_abi)) float ALocation_getSpeed(const ALocation *loc)
{
    return loc ? loc->speed : 0.0f;
}

__attribute__((sysv_abi)) float ALocation_getBearing(const ALocation *loc)
{
    return loc ? loc->bearing : 0.0f;
}

__attribute__((sysv_abi)) int64_t ALocation_getTime(const ALocation *loc)
{
    return loc ? loc->time_ms : 0;
}

__attribute__((sysv_abi)) int64_t ALocation_getElapsedRealtimeNanos(const ALocation *loc)
{
    return loc ? loc->elapsed_realtime_ns : 0;
}

__attribute__((sysv_abi)) int ALocation_hasAltitude(const ALocation *loc)
{
    return (loc && loc->has_altitude) ? 1 : 0;
}

__attribute__((sysv_abi)) int ALocation_hasSpeed(const ALocation *loc)
{
    return (loc && loc->has_speed) ? 1 : 0;
}

__attribute__((sysv_abi)) int ALocation_hasBearing(const ALocation *loc)
{
    return (loc && loc->has_bearing) ? 1 : 0;
}

__attribute__((sysv_abi)) int ALocation_hasAccuracy(const ALocation *loc)
{
    return (loc && loc->has_accuracy) ? 1 : 0;
}

__attribute__((sysv_abi)) void ALocation_destroy(ALocation *loc) { delete loc; }

__attribute__((sysv_abi)) int
AGeofenceManager_addCircularGeofence(ALocationManager *, int fence_id, double lat, double lon,
                                     float radius_m, int64_t expiration_ms,
                                     int64_t dwell_duration_ms, uint32_t transition_mask,
                                     AGeofenceListener_onTransition callback, void *context)
{
    if (!callback) {
        return -1;
    }
    bool ok = LocationManager::Instance().AddCircularGeofence(fence_id, lat, lon, radius_m,
                                                              expiration_ms, dwell_duration_ms,
                                                              transition_mask, callback, context);
    return ok ? 0 : -1;
}

__attribute__((sysv_abi)) int AGeofenceManager_removeGeofence(ALocationManager *, int fence_id)
{
    bool ok = LocationManager::Instance().RemoveGeofence(fence_id);
    return ok ? 0 : -1;
}

} // extern "C"
