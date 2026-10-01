package org.dromara.web.riderguard;

import org.springframework.stereotype.Component;

@Component
public class MockTrafficSignalProvider implements TrafficSignalProvider {
    public static final double LAT = 34.2313;
    public static final double LNG = 108.93555;
    public static final String INTERSECTION_ID = "DEMO-001";
    private static final long CYCLE_MS = 60_000;

    @Override
    public TrafficSignalService.Signal query(TrafficSignalQuery query) {
        long nowMs = query.observedAtMs();
        long phase = Math.floorMod(nowMs, CYCLE_MS);
        String state = phase < 30_000 ? "RED" : phase < 55_000 ? "GREEN" : "YELLOW";
        long phaseEnd = nowMs + (phase < 30_000 ? 30_000 - phase : phase < 55_000 ? 55_000 - phase : CYCLE_MS - phase);
        int seconds = (int) Math.ceil((phaseEnd - nowMs) / 1000.0);
        return new TrafficSignalService.Signal(true, INTERSECTION_ID, "DEMO-NE-STRAIGHT", "STRAIGHT",
            state, seconds, query.distanceM(), "MOCK", true, nowMs, Math.min(nowMs + 3_000, phaseEnd));
    }
}
