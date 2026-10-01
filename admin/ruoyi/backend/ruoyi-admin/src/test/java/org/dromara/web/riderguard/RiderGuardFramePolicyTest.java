package org.dromara.web.riderguard;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

class RiderGuardFramePolicyTest {
    private static final long NOW = 1_000_000L;

    @Test
    void acceptsOnlyFreshFramesNewerThanCurrentState() {
        assertTrue(RiderGuardFramePolicy.isLive(NOW - 4_000, NOW - 8_000, NOW));
        assertFalse(RiderGuardFramePolicy.isLive(NOW - 16_000, NOW - 30_000, NOW));
        assertFalse(RiderGuardFramePolicy.isLive(NOW - 4_000, NOW - 3_000, NOW));
        assertFalse(RiderGuardFramePolicy.isLive(NOW + 6_000, 0, NOW));
    }

    @Test
    void acceptsBoundaryValues() {
        assertTrue(RiderGuardFramePolicy.isLive(NOW - 15_000, 0, NOW));
        assertTrue(RiderGuardFramePolicy.isLive(NOW + 5_000, 0, NOW));
    }
}
