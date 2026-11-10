package org.dromara.web.riderguard;

import jakarta.annotation.PostConstruct;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

/** Shared device response and admin display defaults; independent of inference timeout. */
@Component
public class RiderGuardSampling {
    @Value("${RIDERGUARD_TELEMETRY_INTERVAL_MS:3000}")
    private int telemetryIntervalMs;
    @Value("${RIDERGUARD_IMAGE_INTERVAL_MS:30000}")
    private int imageIntervalMs;
    @Value("${RIDERGUARD_VISION_STALE_AFTER_MS:90000}")
    private int visionStaleAfterMs;

    @PostConstruct
    void validate() {
        if (telemetryIntervalMs < 1000 || telemetryIntervalMs > 60000 || imageIntervalMs < 3000 || imageIntervalMs > 300000
            || visionStaleAfterMs < imageIntervalMs * 2 || visionStaleAfterMs > 900000)
            throw new IllegalStateException("Invalid RiderGuard sampling intervals");
    }

    public int telemetryIntervalMs() { return telemetryIntervalMs; }
    public int imageIntervalMs() { return imageIntervalMs; }
    public int visionStaleAfterMs() { return visionStaleAfterMs; }
}
