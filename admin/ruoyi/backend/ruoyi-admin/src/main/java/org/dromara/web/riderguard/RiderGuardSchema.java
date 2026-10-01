package org.dromara.web.riderguard;

import jakarta.annotation.PostConstruct;
import lombok.RequiredArgsConstructor;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Component;

/** RiderGuard schema bootstrap and journaled, restartable upgrades. */
@Component
@RequiredArgsConstructor
public class RiderGuardSchema {
    private final JdbcTemplate jdbc;

    @PostConstruct
    public void initialize() {
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_schema_migration (
              version INT NOT NULL PRIMARY KEY,
              description VARCHAR(160) NOT NULL,
              applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_rider (
              id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
              name VARCHAR(100) NOT NULL,
              phone VARCHAR(32),
              created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_device (
              device_id VARCHAR(64) NOT NULL PRIMARY KEY,
              rider_id BIGINT NULL,
              token_hash CHAR(64) NOT NULL,
              last_seen_ms BIGINT NULL,
              last_sample_ms BIGINT NULL,
              last_lat DOUBLE NULL,
              last_lng DOUBLE NULL,
              last_speed DOUBLE NULL,
              gps_valid BOOLEAN NOT NULL DEFAULT FALSE,
              crowd_mode BOOLEAN NOT NULL DEFAULT FALSE,
              dense_streak INT NOT NULL DEFAULT 0,
              clear_streak INT NOT NULL DEFAULT 0,
              last_image_ms BIGINT NULL,
              last_image_captured_ms BIGINT NULL,
              last_alert_ms BIGINT NULL,
              created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
              INDEX idx_rg_device_rider (rider_id)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_track (
              id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
              device_id VARCHAR(64) NOT NULL,
              sample_id VARCHAR(80) NOT NULL,
              captured_ms BIGINT NOT NULL,
              lat DOUBLE NULL,
              lng DOUBLE NULL,
              speed_kph DOUBLE NOT NULL,
              gps_valid BOOLEAN NOT NULL,
              heading DOUBLE NULL,
              gps_accuracy DOUBLE NULL,
              created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
              UNIQUE KEY uq_rg_track_sample (device_id, sample_id),
              INDEX idx_rg_track_device_time (device_id, captured_ms)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_image (
              id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
              device_id VARCHAR(64) NOT NULL,
              sample_id VARCHAR(80) NOT NULL,
              captured_ms BIGINT NOT NULL,
              file_name VARCHAR(100) NOT NULL,
              person_count INT NULL,
              inference_status VARCHAR(24) NOT NULL DEFAULT 'PENDING',
              inference_mode VARCHAR(24) NULL,
              created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
              UNIQUE KEY uq_rg_image_sample (device_id, sample_id),
              INDEX idx_rg_image_device_time (device_id, captured_ms)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_event (
              id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
              device_id VARCHAR(64) NOT NULL,
              track_id BIGINT NULL,
              image_id BIGINT NULL,
              event_type VARCHAR(32) NOT NULL,
              crowd_mode BOOLEAN NOT NULL,
              speed_kph DOUBLE NULL,
              speed_limit_kph DOUBLE NULL,
              intersection_id VARCHAR(100) NULL,
              signal_state VARCHAR(16) NULL,
              distance_m DOUBLE NULL,
              signal_source VARCHAR(32) NULL,
              is_mock BOOLEAN NOT NULL DEFAULT FALSE,
              lat DOUBLE NULL,
              lng DOUBLE NULL,
              captured_ms BIGINT NOT NULL,
              status VARCHAR(24) NOT NULL DEFAULT 'OPEN',
              created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
              UNIQUE KEY uq_rg_event_track_type (track_id,event_type),
              UNIQUE KEY uq_rg_event_image_type (image_id,event_type),
              INDEX idx_rg_event_time (captured_ms)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_policy (
              id INT NOT NULL PRIMARY KEY,
              normal_limit_kph DOUBLE NOT NULL,
              crowd_limit_kph DOUBLE NOT NULL,
              crowd_person_count INT NOT NULL,
              confidence_threshold DOUBLE NOT NULL,
              alert_cooldown_ms BIGINT NOT NULL,
              updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_event_action (
              id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
              event_id BIGINT NOT NULL,
              actor_user_id BIGINT NOT NULL,
              actor_name VARCHAR(100) NOT NULL,
              old_status VARCHAR(24) NOT NULL,
              new_status VARCHAR(24) NOT NULL,
              note VARCHAR(500) NOT NULL,
              created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
              INDEX idx_rg_event_action_event (event_id, id)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_inference_attempt (
              id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
              image_id BIGINT NOT NULL,
              result VARCHAR(16) NOT NULL,
              inference_mode VARCHAR(24) NULL,
              duration_ms BIGINT NOT NULL,
              created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
              INDEX idx_rg_inference_attempt_image (image_id),
              INDEX idx_rg_inference_attempt_time (created_at)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
        jdbc.update("INSERT IGNORE INTO rg_policy (id,normal_limit_kph,crowd_limit_kph,crowd_person_count,confidence_threshold,alert_cooldown_ms) VALUES (1,25,10,3,0.5,10000)");
        applyVersion(1, "RiderGuard base schema", () -> {});
        applyVersion(2, "Traffic signal telemetry and events", this::migrateTrafficSignal);
        applyVersion(3, "Image-only crowd events", this::migrateCrowdEvents);
        applyVersion(4, "Ordered live image state", this::migrateImageOrder);
    }

    private void applyVersion(int version, String description, Runnable upgrade) {
        Integer applied = jdbc.queryForObject("SELECT COUNT(*) FROM rg_schema_migration WHERE version=?", Integer.class, version);
        if (applied != null && applied > 0) return;
        // MySQL DDL commits implicitly. Each step checks metadata so a failed startup can safely resume.
        upgrade.run();
        jdbc.update("INSERT IGNORE INTO rg_schema_migration (version,description) VALUES (?,?)", version, description);
    }

    private void migrateTrafficSignal() {
        addColumn("rg_track", "heading", "DOUBLE NULL");
        addColumn("rg_track", "gps_accuracy", "DOUBLE NULL");
        addColumn("rg_event", "intersection_id", "VARCHAR(100) NULL");
        addColumn("rg_event", "signal_state", "VARCHAR(16) NULL");
        addColumn("rg_event", "distance_m", "DOUBLE NULL");
        addColumn("rg_event", "signal_source", "VARCHAR(32) NULL");
        addColumn("rg_event", "is_mock", "BOOLEAN NOT NULL DEFAULT FALSE");
        if (indexExists("rg_event", "uq_rg_event_track")) jdbc.execute("ALTER TABLE rg_event DROP INDEX uq_rg_event_track");
        if (!indexExists("rg_event", "uq_rg_event_track_type"))
            jdbc.execute("ALTER TABLE rg_event ADD UNIQUE KEY uq_rg_event_track_type (track_id,event_type)");
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_traffic_signal_event (
              id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
              risk_event_id BIGINT NOT NULL,
              device_id VARCHAR(64) NOT NULL,
              intersection_id VARCHAR(100) NOT NULL,
              signal_group VARCHAR(100) NOT NULL,
              movement VARCHAR(32) NOT NULL,
              signal_state VARCHAR(16) NOT NULL,
              remaining_seconds INT NULL,
              distance_m DOUBLE NULL,
              source VARCHAR(32) NOT NULL,
              is_mock BOOLEAN NOT NULL,
              observed_ms BIGINT NOT NULL,
              expires_ms BIGINT NOT NULL,
              created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
              UNIQUE KEY uq_rg_signal_risk (risk_event_id),
              INDEX idx_rg_signal_device_time (device_id, observed_ms)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
            """);
    }

    private void migrateCrowdEvents() {
        jdbc.execute("ALTER TABLE rg_event MODIFY track_id BIGINT NULL");
        jdbc.execute("ALTER TABLE rg_event MODIFY speed_kph DOUBLE NULL");
        jdbc.execute("ALTER TABLE rg_event MODIFY speed_limit_kph DOUBLE NULL");
        if (!indexExists("rg_event", "uq_rg_event_image_type"))
            jdbc.execute("ALTER TABLE rg_event ADD UNIQUE KEY uq_rg_event_image_type (image_id,event_type)");
    }

    private void migrateImageOrder() {
        addColumn("rg_device", "last_image_captured_ms", "BIGINT NULL");
    }

    private void addColumn(String table, String column, String definition) {
        Integer count = jdbc.queryForObject("SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name=? AND column_name=?",
            Integer.class, table, column);
        if (count == null || count == 0) jdbc.execute("ALTER TABLE " + table + " ADD COLUMN " + column + " " + definition);
    }

    private boolean indexExists(String table, String index) {
        Integer count = jdbc.queryForObject("SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name=? AND index_name=?",
            Integer.class, table, index);
        return count != null && count > 0;
    }
}
