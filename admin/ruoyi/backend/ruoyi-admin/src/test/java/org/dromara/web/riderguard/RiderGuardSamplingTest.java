package org.dromara.web.riderguard;

import org.junit.jupiter.api.Test;
import org.springframework.test.util.ReflectionTestUtils;
import static org.junit.jupiter.api.Assertions.*;

class RiderGuardSamplingTest {
    private RiderGuardSampling sampling(int telemetry, int image, int stale) {
        RiderGuardSampling value = new RiderGuardSampling();
        ReflectionTestUtils.setField(value, "telemetryIntervalMs", telemetry);
        ReflectionTestUtils.setField(value, "imageIntervalMs", image);
        ReflectionTestUtils.setField(value, "visionStaleAfterMs", stale);
        return value;
    }

    @Test void thirtySecondImagesRetainAUsableFreshnessWindow() {
        RiderGuardSampling value = sampling(3000, 30000, 90000);
        assertDoesNotThrow(value::validate);
        assertEquals(30000, value.imageIntervalMs());
        assertTrue(value.visionStaleAfterMs() >= value.imageIntervalMs() * 2);
    }

    @Test void rejectsAWindowThatExpiresBeforeTheNextImage() {
        assertThrows(IllegalStateException.class, sampling(3000, 30000, 15000)::validate);
        assertThrows(IllegalStateException.class, sampling(0, 30000, 90000)::validate);
    }

    @Test void usbWorkerIsUnavailableUnlessExplicitlyEnabled() {
        RiderGuardLocalDeviceService local = new RiderGuardLocalDeviceService(sampling(3000, 30000, 90000));
        assertEquals(false, local.status().get("available"));
        assertEquals(false, local.status().get("running"));
        assertThrows(org.springframework.web.server.ResponseStatusException.class, () -> local.start("SG-001", "COM3", "a".repeat(64)));
        assertThrows(org.springframework.web.server.ResponseStatusException.class, () -> local.command(null, null));
        assertEquals(false, local.stop().get("running"));
    }
}
