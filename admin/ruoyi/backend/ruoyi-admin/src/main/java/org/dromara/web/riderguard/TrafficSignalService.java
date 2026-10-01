package org.dromara.web.riderguard;

import lombok.RequiredArgsConstructor;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.util.Map;

/** Fixed, explicitly simulated approach. It is never a real-road signal judgment. */
@Service
@RequiredArgsConstructor
public class TrafficSignalService {
    private final MockTrafficSignalProvider provider;
    @Value("${RIDERGUARD_DEMO_MODE:false}")
    private boolean demoMode;

    public record Signal(boolean available, String intersectionId, String signalGroup, String movement,
                         String state, Integer remainingSeconds, Double distanceM, String source,
                         boolean mock, long observedAtMs, long validUntilMs) {}

    public Signal observe(String deviceId, Double latitude, Double longitude, double speedKph,
                          Double heading, Double accuracy, boolean gpsValid, long sampleMs, long nowMs) {
        if (!demoMode) return unknown(nowMs);
        if (!gpsValid || latitude == null || longitude == null || heading == null || accuracy == null ||
            !Double.isFinite(heading) || !Double.isFinite(accuracy) || accuracy > 25 || accuracy < 0 ||
            Math.abs(nowMs - sampleMs) > 5_000) return unknown(nowMs);
        double distance = distance(latitude, longitude, MockTrafficSignalProvider.LAT, MockTrafficSignalProvider.LNG);
        if (distance < 5 || distance > 100) return unknown(nowMs);
        double bearing = bearing(latitude, longitude, MockTrafficSignalProvider.LAT, MockTrafficSignalProvider.LNG);
        double difference = Math.abs(((heading - bearing + 540) % 360) - 180);
        if (difference > 45) return unknown(nowMs);
        return provider.query(new TrafficSignalQuery(deviceId, latitude, longitude, speedKph, heading,
            accuracy, MockTrafficSignalProvider.INTERSECTION_ID, "STRAIGHT", distance, nowMs));
    }

    public boolean warn(Signal signal, double speedKph, long nowMs) {
        return speedKph >= 3 && signal.available() && "RED".equals(signal.state()) && nowMs < signal.validUntilMs();
    }

    public Map<String, Object> demoIntersection() {
        if (!demoMode) return Map.of("enabled", false);
        long now = System.currentTimeMillis();
        return Map.of("enabled", true, "latitude", MockTrafficSignalProvider.LAT,
            "longitude", MockTrafficSignalProvider.LNG,
            "signal", provider.query(new TrafficSignalQuery(null, MockTrafficSignalProvider.LAT,
                MockTrafficSignalProvider.LNG, 0, 51.2, 0, MockTrafficSignalProvider.INTERSECTION_ID,
                "STRAIGHT", 0, now)));
    }

    private Signal unknown(long now) {
        return new Signal(false, null, null, "UNKNOWN", "UNKNOWN", null, null,
            demoMode ? "MOCK" : "UNKNOWN", demoMode, now, now);
    }

    private static double distance(double lat1, double lng1, double lat2, double lng2) {
        double a = Math.sin(Math.toRadians(lat2 - lat1) / 2);
        double b = Math.sin(Math.toRadians(lng2 - lng1) / 2);
        double h = a * a + Math.cos(Math.toRadians(lat1)) * Math.cos(Math.toRadians(lat2)) * b * b;
        return 12_742_000 * Math.asin(Math.min(1, Math.sqrt(h)));
    }

    private static double bearing(double lat1, double lng1, double lat2, double lng2) {
        double delta = Math.toRadians(lng2 - lng1);
        double y = Math.sin(delta) * Math.cos(Math.toRadians(lat2));
        double x = Math.cos(Math.toRadians(lat1)) * Math.sin(Math.toRadians(lat2)) -
            Math.sin(Math.toRadians(lat1)) * Math.cos(Math.toRadians(lat2)) * Math.cos(delta);
        return (Math.toDegrees(Math.atan2(y, x)) + 360) % 360;
    }
}
