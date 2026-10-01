package org.dromara.web.riderguard;

import org.junit.jupiter.api.Test;
import org.springframework.test.util.ReflectionTestUtils;

import static org.junit.jupiter.api.Assertions.*;

class TrafficSignalServiceTest {
    private final TrafficSignalService service = new TrafficSignalService(new MockTrafficSignalProvider());

    TrafficSignalServiceTest() {
        ReflectionTestUtils.setField(service, "demoMode", true);
    }

    @Test
    void matchedApproachWarnsOnlyDuringFreshRedPhase() {
        long redTime = 20_000;
        var signal = service.observe("SG-001", 34.2309, 108.93495, 18, 51.2, 3.2, true, redTime, redTime);
        assertEquals("MOCK", signal.source());
        assertEquals("RED", signal.state());
        assertTrue(service.warn(signal, 18, redTime));
        assertFalse(service.warn(signal, 0, redTime));
        assertFalse(service.warn(signal, 18, signal.validUntilMs()));
        assertTrue(signal.validUntilMs() <= 30_000);
    }

    @Test
    void uncertainOrDepartingLocationCannotWarn() {
        long now = 20_000;
        assertEquals("UNKNOWN", service.observe("SG-001", 34.2309, 108.93495, 18, null, 3.2, true, now, now).state());
        assertEquals("UNKNOWN", service.observe("SG-001", 34.2309, 108.93495, 18, 51.2, 40.0, true, now, now).state());
        assertEquals("UNKNOWN", service.observe("SG-001", 34.2309, 108.93495, 18, 230.0, 3.2, true, now, now).state());
        assertEquals("UNKNOWN", service.observe("SG-001", 34.2309, 108.93495, 18, 51.2, 3.2, true, now - 6000, now).state());
        assertEquals("UNKNOWN", service.observe("SG-001", 34.2309, 108.93495, 18, 51.2, 3.2, false, now, now).state());
    }
}
