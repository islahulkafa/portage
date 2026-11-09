#pragma once

#include "location/location_types.hpp"

#include <cstdint>

struct ALocationManager;

extern "C" {

__attribute__((sysv_abi)) ALocationManager *ALocationManager_create();
__attribute__((sysv_abi)) void ALocationManager_destroy(ALocationManager *mgr);
__attribute__((sysv_abi)) int ALocationManager_isProviderEnabled(ALocationManager *mgr,
                                                                 const char *provider);
__attribute__((sysv_abi)) portage::location::ALocation *ALocationManager_getLastKnownLocation(
    ALocationManager *mgr, const char *provider);
__attribute__((sysv_abi)) int ALocationManager_requestLocationUpdates(
    ALocationManager *mgr, const char *provider, int64_t min_time_ms, float min_distance_m,
    portage::location::ALocationListener_onLocationChanged callback, void *context);
__attribute__((sysv_abi)) int ALocationManager_removeUpdates(
    ALocationManager *mgr, portage::location::ALocationListener_onLocationChanged callback);

__attribute__((sysv_abi)) double ALocation_getLatitude(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) double ALocation_getLongitude(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) double ALocation_getAltitude(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) float ALocation_getAccuracy(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) float ALocation_getSpeed(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) float ALocation_getBearing(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) int64_t ALocation_getTime(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) int64_t ALocation_getElapsedRealtimeNanos(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) int ALocation_hasAltitude(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) int ALocation_hasSpeed(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) int ALocation_hasBearing(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) int ALocation_hasAccuracy(
    const portage::location::ALocation *loc);
__attribute__((sysv_abi)) void ALocation_destroy(portage::location::ALocation *loc);

__attribute__((sysv_abi)) int AGeofenceManager_addCircularGeofence(
    ALocationManager *mgr, int fence_id, double lat, double lon, float radius_m,
    int64_t expiration_ms, int64_t dwell_duration_ms, uint32_t transition_mask,
    portage::location::AGeofenceListener_onTransition callback, void *context);
__attribute__((sysv_abi)) int AGeofenceManager_removeGeofence(ALocationManager *mgr,
                                                               int fence_id);

} // extern "C"
