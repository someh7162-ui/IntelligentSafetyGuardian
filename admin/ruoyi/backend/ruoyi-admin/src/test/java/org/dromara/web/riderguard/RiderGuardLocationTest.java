package org.dromara.web.riderguard;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class RiderGuardLocationTest {
    @Test void acceptsLegacyGpsAndKeepsLbsAgeSeparate() {
        var gps = RiderGuardLocation.select(true, null, null, 30.0, 120.0, 1000000L);
        assertEquals("GPS", gps.source());
        assertEquals(1000000L, gps.observedAtMs());
        var lbs = RiderGuardLocation.select(false, "LBS", 20000L, 30.0, 120.0, 1000000L);
        assertEquals("LBS", lbs.source());
        assertTrue(lbs.hasPosition());
        assertEquals(980000L, lbs.observedAtMs());
        assertFalse(RiderGuardLocation.select(false, null, null, null, null, 1000000L).hasPosition());
    }
    @Test void rejectsStaleMislabelledAndInvalidLocations() {
        assertThrows(RuntimeException.class, () -> RiderGuardLocation.select(true, "LBS", 0L, 30.0, 120.0, 1000000L));
        assertThrows(RuntimeException.class, () -> RiderGuardLocation.select(false, "GPS", 0L, 30.0, 120.0, 1000000L));
        assertThrows(RuntimeException.class, () -> RiderGuardLocation.select(false, "LBS", null, 30.0, 120.0, 1000000L));
        assertThrows(RuntimeException.class, () -> RiderGuardLocation.select(false, "LBS", 600001L, 30.0, 120.0, 1000000L));
        assertThrows(RuntimeException.class, () -> RiderGuardLocation.select(false, "LBS", 0L, Double.NaN, 120.0, 1000000L));
    }
}
