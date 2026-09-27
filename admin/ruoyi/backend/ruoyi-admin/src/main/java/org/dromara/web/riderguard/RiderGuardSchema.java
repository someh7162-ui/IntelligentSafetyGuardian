package org.dromara.web.riderguard;

import jakarta.annotation.PostConstruct;
import lombok.RequiredArgsConstructor;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Component;

/** Idempotent prototype schema. Replace with versioned migrations before production rollout. */
@Component
@RequiredArgsConstructor
public class RiderGuardSchema {
    private final JdbcTemplate jdbc;

    @PostConstruct
    public void initialize() {
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
              track_id BIGINT NOT NULL,
              image_id BIGINT NULL,
              event_type VARCHAR(32) NOT NULL,
              crowd_mode BOOLEAN NOT NULL,
              speed_kph DOUBLE NOT NULL,
              speed_limit_kph DOUBLE NOT NULL,
              lat DOUBLE NULL,
              lng DOUBLE NULL,
              captured_ms BIGINT NOT NULL,
              status VARCHAR(24) NOT NULL DEFAULT 'OPEN',
              created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
              UNIQUE KEY uq_rg_event_track (track_id),
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
    }
}
