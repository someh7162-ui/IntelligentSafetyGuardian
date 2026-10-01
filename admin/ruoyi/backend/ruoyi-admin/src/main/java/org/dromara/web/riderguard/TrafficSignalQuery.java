package org.dromara.web.riderguard;

/** Matched approach context passed to a signal source. */
public record TrafficSignalQuery(String deviceId, double latitude, double longitude, double speedKph,
                                 double heading, double gpsAccuracy, String intersectionId,
                                 String movement, double distanceM, long observedAtMs) {}
