#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ALocationManager;
typedef struct ALocationManager ALocationManager;

struct ALocation;
typedef struct ALocation ALocation;

typedef void (*ALocationListener_onLocationChanged)(const ALocation *location, void *context);
typedef void (*ALocationListener_onStatusChanged)(const char *provider, int status, void *context);
typedef void (*AGeofenceListener_onTransition)(int fence_id, int transition,
                                               const ALocation *location, void *context);

__attribute__((visibility("default"))) ALocationManager *ALocationManager_create(void)
{
    return (ALocationManager *)0;
}

__attribute__((visibility("default"))) void ALocationManager_destroy(ALocationManager *mgr)
{
    (void)mgr;
}

__attribute__((visibility("default"))) int ALocationManager_isProviderEnabled(ALocationManager *mgr,
                                                                              const char *provider)
{
    (void)mgr;
    (void)provider;
    return 0;
}

__attribute__((visibility("default"))) ALocation *
ALocationManager_getLastKnownLocation(ALocationManager *mgr, const char *provider)
{
    (void)mgr;
    (void)provider;
    return (ALocation *)0;
}

__attribute__((visibility("default"))) int
ALocationManager_requestLocationUpdates(ALocationManager *mgr, const char *provider,
                                        int64_t min_time_ms, float min_distance_m,
                                        ALocationListener_onLocationChanged callback, void *context)
{
    (void)mgr;
    (void)provider;
    (void)min_time_ms;
    (void)min_distance_m;
    (void)callback;
    (void)context;
    return 0;
}

__attribute__((visibility("default"))) int
ALocationManager_removeUpdates(ALocationManager *mgr, ALocationListener_onLocationChanged callback)
{
    (void)mgr;
    (void)callback;
    return 0;
}

__attribute__((visibility("default"))) double ALocation_getLatitude(const ALocation *loc)
{
    (void)loc;
    return 0.0;
}

__attribute__((visibility("default"))) double ALocation_getLongitude(const ALocation *loc)
{
    (void)loc;
    return 0.0;
}

__attribute__((visibility("default"))) double ALocation_getAltitude(const ALocation *loc)
{
    (void)loc;
    return 0.0;
}

__attribute__((visibility("default"))) float ALocation_getAccuracy(const ALocation *loc)
{
    (void)loc;
    return 0.0f;
}

__attribute__((visibility("default"))) float ALocation_getSpeed(const ALocation *loc)
{
    (void)loc;
    return 0.0f;
}

__attribute__((visibility("default"))) float ALocation_getBearing(const ALocation *loc)
{
    (void)loc;
    return 0.0f;
}

__attribute__((visibility("default"))) int64_t ALocation_getTime(const ALocation *loc)
{
    (void)loc;
    return 0;
}

__attribute__((visibility("default"))) int64_t
ALocation_getElapsedRealtimeNanos(const ALocation *loc)
{
    (void)loc;
    return 0;
}

__attribute__((visibility("default"))) int ALocation_hasAltitude(const ALocation *loc)
{
    (void)loc;
    return 0;
}

__attribute__((visibility("default"))) int ALocation_hasSpeed(const ALocation *loc)
{
    (void)loc;
    return 0;
}

__attribute__((visibility("default"))) int ALocation_hasBearing(const ALocation *loc)
{
    (void)loc;
    return 0;
}

__attribute__((visibility("default"))) int ALocation_hasAccuracy(const ALocation *loc)
{
    (void)loc;
    return 0;
}

__attribute__((visibility("default"))) void ALocation_destroy(ALocation *loc) { (void)loc; }

__attribute__((visibility("default"))) int
AGeofenceManager_addCircularGeofence(ALocationManager *mgr, int fence_id, double lat, double lon,
                                     float radius_m, int64_t expiration_ms,
                                     int64_t dwell_duration_ms, uint32_t transition_mask,
                                     AGeofenceListener_onTransition callback, void *context)
{
    (void)mgr;
    (void)fence_id;
    (void)lat;
    (void)lon;
    (void)radius_m;
    (void)expiration_ms;
    (void)dwell_duration_ms;
    (void)transition_mask;
    (void)callback;
    (void)context;
    return 0;
}

__attribute__((visibility("default"))) int AGeofenceManager_removeGeofence(ALocationManager *mgr,
                                                                           int fence_id)
{
    (void)mgr;
    (void)fence_id;
    return 0;
}
