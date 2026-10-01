package org.dromara.web.riderguard;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import lombok.RequiredArgsConstructor;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Component;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Deletes old ordinary photos without touching risk evidence. */
@Component
@RequiredArgsConstructor
public class RiderGuardImageRetention {
    private static final Logger LOG = LoggerFactory.getLogger(RiderGuardImageRetention.class);
    private final JdbcTemplate jdbc;

    @Value("${RIDERGUARD_IMAGE_DIR:deploy/local-runtime/uploads}")
    private String imageDirectory;
    @Value("${RIDERGUARD_IMAGE_CLEANUP_ENABLED:false}")
    private boolean enabled;
    @Value("${RIDERGUARD_IMAGE_RETENTION_DAYS:3}")
    private int retentionDays;
    @Value("${RIDERGUARD_IMAGE_CLEANUP_BATCH_SIZE:500}")
    private int batchSize;

    private ScheduledExecutorService scheduler;

    @PostConstruct
    public void start() {
        if (!enabled) return;
        if (retentionDays < 1 || batchSize < 1 || batchSize > 1000) {
            throw new IllegalArgumentException("Invalid RiderGuard image retention settings");
        }
        scheduler = Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "riderguard-image-retention");
            thread.setDaemon(true);
            return thread;
        });
        scheduler.scheduleWithFixedDelay(this::runSafely, 5, 15, TimeUnit.MINUTES);
    }

    @PreDestroy
    public void stop() {
        if (scheduler != null) scheduler.shutdownNow();
    }

    private void runSafely() {
        try {
            int removed = cleanupOnce();
            if (removed > 0) LOG.info("RiderGuard removed {} expired ordinary images", removed);
        } catch (RuntimeException error) {
            LOG.error("RiderGuard image cleanup failed; it will retry later", error);
        }
    }

    int cleanupOnce() {
        long cutoffMs = System.currentTimeMillis() - Duration.ofDays(retentionDays).toMillis();
        Path root = Path.of(imageDirectory).toAbsolutePath().normalize();
        List<Map<String, Object>> candidates = jdbc.queryForList("""
            SELECT i.id,i.file_name FROM rg_image i
            WHERE i.captured_ms < ?
              AND NOT EXISTS (SELECT 1 FROM rg_event e WHERE e.image_id=i.id)
            ORDER BY i.captured_ms,i.id LIMIT ?
            """, cutoffMs, batchSize);
        int removed = 0;
        for (Map<String, Object> candidate : candidates) {
            long id = ((Number) candidate.get("id")).longValue();
            String fileName = (String) candidate.get("file_name");
            if (!isStoredImageName(fileName)) {
                LOG.warn("Skipping image {} with unexpected stored filename", id);
                continue;
            }
            // Recheck evidence in the DELETE so a newly linked event cannot lose its database image.
            int deleted = jdbc.update("DELETE FROM rg_image WHERE id=? AND NOT EXISTS " +
                "(SELECT 1 FROM rg_event WHERE image_id=?)", id, id);
            if (deleted == 0) continue;
            jdbc.update("DELETE FROM rg_inference_attempt WHERE image_id=?", id);
            try {
                Files.deleteIfExists(root.resolve(fileName));
            } catch (IOException error) {
                LOG.error("Expired image {} was removed from the database, but its file remains", id, error);
            }
            removed++;
        }
        return removed;
    }

    private boolean isStoredImageName(String value) {
        if (value == null || !value.endsWith(".jpg")) return false;
        String uuid = value.substring(0, value.length() - 4);
        try {
            return UUID.fromString(uuid).toString().equals(uuid);
        } catch (IllegalArgumentException error) {
            return false;
        }
    }
}
