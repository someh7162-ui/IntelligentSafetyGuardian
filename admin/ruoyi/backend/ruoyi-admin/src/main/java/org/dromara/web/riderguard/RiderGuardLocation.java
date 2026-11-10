package org.dromara.web.riderguard;

import org.springframework.http.HttpStatus;
import org.springframework.web.server.ResponseStatusException;

/** A coarse cell location never becomes a GNSS fix or a speed measurement. */
final class RiderGuardLocation {
    static final long MAX_LBS_AGE_MS = 600_000L;
    record Fix(String source, boolean hasPosition, long observedAtMs) {}

    static Fix select(boolean gpsValid, String supplied, Long ageMs, Double latitude, Double longitude, long capturedMs) {
        String source = supplied == null ? (gpsValid ? "GPS" : "NONE") : supplied;
        if (!source.equals("GPS") && !source.equals("LBS") && !source.equals("NONE")) invalid();
        if (gpsValid != source.equals("GPS")) invalid();
        if (source.equals("NONE")) return new Fix(source, false, 0);
        if (latitude == null || longitude == null || !Double.isFinite(latitude) || !Double.isFinite(longitude)
            || Math.abs(latitude) > 90 || Math.abs(longitude) > 180) invalid();
        if (source.equals("LBS") && (ageMs == null || ageMs < 0 || ageMs > MAX_LBS_AGE_MS)) invalid();
        long age = source.equals("LBS") ? ageMs : 0;
        return new Fix(source, true, capturedMs - age);
    }

    private static void invalid() {
        throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Invalid location source, age or coordinates");
    }
}
