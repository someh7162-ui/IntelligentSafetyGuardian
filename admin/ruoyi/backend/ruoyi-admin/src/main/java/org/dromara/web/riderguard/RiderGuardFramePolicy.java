package org.dromara.web.riderguard;

/** Decides whether a captured JPEG may change the current rider risk state. */
final class RiderGuardFramePolicy {
    private static final long MAX_AGE_MS = 15_000L;
    private static final long MAX_CLOCK_SKEW_MS = 5_000L;

    private RiderGuardFramePolicy() {}

    static boolean isLive(long capturedMs, long lastLiveCapturedMs, long nowMs) {
        return capturedMs > lastLiveCapturedMs
            && capturedMs >= nowMs - MAX_AGE_MS
            && capturedMs <= nowMs + MAX_CLOCK_SKEW_MS;
    }
}
