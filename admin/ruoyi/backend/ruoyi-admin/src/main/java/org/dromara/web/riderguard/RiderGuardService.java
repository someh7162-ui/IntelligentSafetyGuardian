package org.dromara.web.riderguard;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import lombok.RequiredArgsConstructor;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpStatus;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.support.GeneratedKeyHolder;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.server.ResponseStatusException;

import java.io.IOException;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.sql.PreparedStatement;
import java.sql.Statement;
import java.time.Duration;
import java.util.HexFormat;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.locks.ReentrantLock;

@Service
@RequiredArgsConstructor
public class RiderGuardService {
    private final JdbcTemplate jdbc;
    private final ObjectMapper mapper = new ObjectMapper();
    private final SecureRandom random = new SecureRandom();
    private final HttpClient http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(2)).build();
    private final ConcurrentHashMap<String, ReentrantLock> imageLocks = new ConcurrentHashMap<>();

    @Value("${RIDERGUARD_IMAGE_DIR:deploy/local-runtime/uploads}")
    private String imageDirectory;
    @Value("${RIDERGUARD_AI_URL:http://127.0.0.1:8091}")
    private String aiUrl;
    @Value("${RIDERGUARD_AI_KEY:}")
    private String aiKey;
    @Value("${RIDERGUARD_DEMO_MODE:false}")
    private boolean demoMode;

    public record Telemetry(String deviceId, String sampleId, long capturedAtMs, Double latitude,
                            Double longitude, double speedKph, boolean gpsValid) {}

    public synchronized Map<String, Object> telemetry(Telemetry value, String token) {
        authorize(value.deviceId(), token);
        if (value.sampleId() == null || !value.sampleId().matches("[A-Za-z0-9_-]{1,80}")) bad("无效采样编号");
        if (value.capturedAtMs() < System.currentTimeMillis() - 86_400_000L || value.capturedAtMs() > System.currentTimeMillis() + 300_000L) bad("采样时间不在允许范围");
        if (!Double.isFinite(value.speedKph()) || value.speedKph() < 0 || value.speedKph() > 150) bad("无效速度");
        if (value.gpsValid() && (value.latitude() == null || value.longitude() == null ||
            !Double.isFinite(value.latitude()) || !Double.isFinite(value.longitude()) ||
            Math.abs(value.latitude()) > 90 || Math.abs(value.longitude()) > 180)) bad("无效定位");
        Map<String, Object> device = device(value.deviceId());
        boolean fresh = value.capturedAtMs() >= number(device.get("last_sample_ms"), 0);
        int inserted = jdbc.update("INSERT IGNORE INTO rg_track (device_id,sample_id,captured_ms,lat,lng,speed_kph,gps_valid) VALUES (?,?,?,?,?,?,?)",
            value.deviceId(), value.sampleId(), value.capturedAtMs(), value.gpsValid() ? value.latitude() : null,
            value.gpsValid() ? value.longitude() : null, value.speedKph(), value.gpsValid());
        if (fresh && inserted > 0) {
            jdbc.update("UPDATE rg_device SET last_seen_ms=?,last_sample_ms=?,last_lat=?,last_lng=?,last_speed=?,gps_valid=? WHERE device_id=?",
                System.currentTimeMillis(), value.capturedAtMs(), value.gpsValid() ? value.latitude() : null,
                value.gpsValid() ? value.longitude() : null, value.speedKph(), value.gpsValid(), value.deviceId());
        } else {
            jdbc.update("UPDATE rg_device SET last_seen_ms=? WHERE device_id=?", System.currentTimeMillis(), value.deviceId());
        }
        Map<String, Object> policy = policy();
        boolean crowd = Boolean.TRUE.equals(device.get("crowd_mode")) || number(device.get("crowd_mode"), 0) == 1;
        double limit = number(policy.get(crowd ? "crowd_limit_kph" : "normal_limit_kph"), 25);
        boolean overspeed = inserted > 0 && value.speedKph() > limit;
        if (overspeed) {
            Long lastAlert = jdbc.queryForObject("SELECT COALESCE(MAX(captured_ms),0) FROM rg_event WHERE device_id=?", Long.class, value.deviceId());
            if (value.capturedAtMs() - lastAlert >= number(policy.get("alert_cooldown_ms"), 10000)) {
                long trackId = jdbc.queryForObject("SELECT id FROM rg_track WHERE device_id=? AND sample_id=?", Long.class, value.deviceId(), value.sampleId());
                List<Long> images = jdbc.query("SELECT id FROM rg_image WHERE device_id=? AND inference_status='READY' AND captured_ms BETWEEN ? AND ? ORDER BY captured_ms DESC LIMIT 1",
                    (rs, rowNum) -> rs.getLong(1), value.deviceId(), value.capturedAtMs() - 15000, value.capturedAtMs() + 1000);
                jdbc.update("INSERT IGNORE INTO rg_event (device_id,track_id,image_id,event_type,crowd_mode,speed_kph,speed_limit_kph,lat,lng,captured_ms) VALUES (?,?,?,?,?,?,?,?,?,?)",
                    value.deviceId(), trackId, images.isEmpty() ? null : images.getFirst(), "OVERSPEED", crowd, value.speedKph(), limit,
                    value.gpsValid() ? value.latitude() : null, value.gpsValid() ? value.longitude() : null, value.capturedAtMs());
                jdbc.update("UPDATE rg_device SET last_alert_ms=? WHERE device_id=?", System.currentTimeMillis(), value.deviceId());
            }
        }
        boolean visionFresh = number(device.get("last_image_ms"), 0) >= System.currentTimeMillis() - 15000;
        return Map.of("mode", crowd ? "CROWD" : "NORMAL", "limitKph", limit,
            "overspeed", value.speedKph() > limit, "alert", overspeed,
            "validUntilMs", System.currentTimeMillis() + 15000, "imageIntervalMs", 4000,
            "visionStatus", visionFresh ? "READY" : "STALE");
    }

    public Map<String, Object> image(String deviceId, String sampleId, long capturedMs,
                                                   byte[] bytes, String token, Integer demoPeople) {
        authorize(deviceId, token);
        if (sampleId == null || !sampleId.matches("[A-Za-z0-9_-]{1,80}")) bad("无效图片编号");
        if (capturedMs < System.currentTimeMillis() - 86_400_000L || capturedMs > System.currentTimeMillis() + 300_000L) bad("图片时间不在允许范围");
        if (bytes.length < 4 || bytes.length > 1_048_576 || (bytes[0] & 0xff) != 0xff || (bytes[1] & 0xff) != 0xd8 ||
            (bytes[bytes.length - 2] & 0xff) != 0xff || (bytes[bytes.length - 1] & 0xff) != 0xd9) bad("仅接受不超过 1MB 的完整 JPEG");
        if (demoPeople != null && (!demoMode || demoPeople < 0 || demoPeople > 100)) bad("模拟人数仅限演示环境");
        // Preserve photo order per device while keeping inference off the telemetry monitor.
        ReentrantLock imageLock = imageLocks.computeIfAbsent(deviceId, ignored -> new ReentrantLock());
        imageLock.lock();
        try {
        long imageId;
        synchronized (this) {
            List<Map<String, Object>> existing = jdbc.queryForList("SELECT id,inference_status,person_count,UNIX_TIMESTAMP(created_at)*1000 AS created_ms FROM rg_image WHERE device_id=? AND sample_id=?", deviceId, sampleId);
            if (!existing.isEmpty()) {
                Map<String, Object> previous = existing.getFirst();
                if ("READY".equals(previous.get("inference_status"))) return previous;
                imageId = number(previous.get("id"), 0);
                if ("PROCESSING".equals(previous.get("inference_status")) &&
                    System.currentTimeMillis() - number(previous.get("created_ms"), 0) < 30000) {
                    return Map.of("imageId", imageId, "status", "PROCESSING");
                }
                bytes = image(imageId);
            } else {
                String fileName = UUID.randomUUID() + ".jpg";
                Path file = Path.of(imageDirectory).toAbsolutePath().normalize().resolve(fileName);
                try {
                    Files.createDirectories(file.getParent());
                    Files.write(file, bytes);
                } catch (IOException error) {
                    throw new ResponseStatusException(HttpStatus.INTERNAL_SERVER_ERROR, "图片保存失败", error);
                }
                imageId = insertImage(deviceId, sampleId, capturedMs, fileName);
            }
            jdbc.update("UPDATE rg_image SET inference_status='PROCESSING' WHERE id=?", imageId);
        }
        long startedAt = System.nanoTime();
        try {
            String query = demoPeople == null ? "" : "?hint=" + URLEncoder.encode(demoPeople.toString(), StandardCharsets.UTF_8);
            HttpRequest request = HttpRequest.newBuilder(URI.create(aiUrl + "/infer/crowd" + query))
                .timeout(Duration.ofSeconds(8)).header("Content-Type", "image/jpeg").header("X-Internal-Key", aiKey)
                .POST(HttpRequest.BodyPublishers.ofByteArray(bytes)).build();
            HttpResponse<String> result = http.send(request, HttpResponse.BodyHandlers.ofString());
            if (result.statusCode() != 200) throw new IOException("AI service returned " + result.statusCode());
            JsonNode payload = mapper.readTree(result.body());
            int count = payload.path("personCount").asInt(-1);
            if (count < 0) throw new IOException("AI response lacks personCount");
            String mode = payload.path("mode").asText("unknown");
            jdbc.update("UPDATE rg_image SET person_count=?,inference_status='READY',inference_mode=? WHERE id=?", count, mode, imageId);
            synchronized (this) { updateCrowd(deviceId, count); }
            jdbc.update("UPDATE rg_event SET image_id=? WHERE device_id=? AND image_id IS NULL AND captured_ms BETWEEN ? AND ?",
                imageId, deviceId, capturedMs - 1000, capturedMs + 15000);
            jdbc.update("INSERT INTO rg_inference_attempt (image_id,result,inference_mode,duration_ms) VALUES (?,?,?,?)",
                imageId, "READY", mode, Duration.ofNanos(System.nanoTime() - startedAt).toMillis());
            return Map.of("imageId", imageId, "personCount", count, "inferenceMode", mode, "status", "READY");
        } catch (IOException | InterruptedException | IllegalArgumentException error) {
            if (error instanceof InterruptedException) Thread.currentThread().interrupt();
            jdbc.update("UPDATE rg_image SET inference_status='ERROR' WHERE id=?", imageId);
            jdbc.update("INSERT INTO rg_inference_attempt (image_id,result,duration_ms) VALUES (?,?,?)",
                imageId, "ERROR", Duration.ofNanos(System.nanoTime() - startedAt).toMillis());
            return Map.of("imageId", imageId, "status", "ERROR", "message", "识别服务不可用，图片已保存");
        }
        } finally {
            imageLock.unlock();
        }
    }

    private void updateCrowd(String deviceId, int people) {
        Map<String, Object> d = device(deviceId);
        boolean dense = people >= number(policy().get("crowd_person_count"), 3);
        int denseStreak = dense ? (int) number(d.get("dense_streak"), 0) + 1 : 0;
        int clearStreak = dense ? 0 : (int) number(d.get("clear_streak"), 0) + 1;
        boolean crowd = number(d.get("crowd_mode"), 0) == 1 || Boolean.TRUE.equals(d.get("crowd_mode"));
        if (denseStreak >= 2) crowd = true;
        if (clearStreak >= 3) crowd = false;
        jdbc.update("UPDATE rg_device SET dense_streak=?,clear_streak=?,crowd_mode=?,last_image_ms=? WHERE device_id=?",
            denseStreak, clearStreak, crowd, System.currentTimeMillis(), deviceId);
    }

    public Map<String, Object> provision(String deviceId, Long riderId) {
        if (deviceId == null || !deviceId.matches("[A-Za-z0-9_-]{3,64}")) bad("设备编号需为 3～64 位字母、数字、下划线或横线");
        if (riderId != null && jdbc.queryForObject("SELECT COUNT(*) FROM rg_rider WHERE id=?", Integer.class, riderId) == 0) bad("骑手不存在");
        byte[] secret = new byte[32];
        random.nextBytes(secret);
        String token = HexFormat.of().formatHex(secret);
        int changed = jdbc.update("INSERT IGNORE INTO rg_device (device_id,rider_id,token_hash) VALUES (?,?,?)", deviceId, riderId, sha256(token));
        if (changed == 0) throw new ResponseStatusException(HttpStatus.CONFLICT, "设备编号已存在");
        return Map.of("deviceId", deviceId, "token", token);
    }

    public Map<String, Object> rotateToken(String deviceId) {
        device(deviceId);
        byte[] secret = new byte[32];
        random.nextBytes(secret);
        String token = HexFormat.of().formatHex(secret);
        jdbc.update("UPDATE rg_device SET token_hash=? WHERE device_id=?", sha256(token), deviceId);
        return Map.of("deviceId", deviceId, "token", token);
    }

    public void bind(String deviceId, Long riderId) {
        device(deviceId);
        if (riderId != null && jdbc.queryForObject("SELECT COUNT(*) FROM rg_rider WHERE id=?", Integer.class, riderId) == 0) bad("骑手不存在");
        jdbc.update("UPDATE rg_device SET rider_id=? WHERE device_id=?", riderId, deviceId);
    }

    public long addRider(String name, String phone) {
        if (name == null || name.isBlank() || name.length() > 100) bad("骑手姓名无效");
        GeneratedKeyHolder keys = new GeneratedKeyHolder();
        jdbc.update(connection -> {
            PreparedStatement stmt = connection.prepareStatement("INSERT INTO rg_rider (name,phone) VALUES (?,?)", Statement.RETURN_GENERATED_KEYS);
            stmt.setString(1, name.strip());
            stmt.setString(2, phone);
            return stmt;
        }, keys);
        return keys.getKey().longValue();
    }

    private long insertImage(String deviceId, String sampleId, long capturedMs, String fileName) {
        GeneratedKeyHolder keys = new GeneratedKeyHolder();
        jdbc.update(connection -> {
            PreparedStatement stmt = connection.prepareStatement("INSERT INTO rg_image (device_id,sample_id,captured_ms,file_name) VALUES (?,?,?,?)", Statement.RETURN_GENERATED_KEYS);
            stmt.setString(1, deviceId);
            stmt.setString(2, sampleId);
            stmt.setLong(3, capturedMs);
            stmt.setString(4, fileName);
            return stmt;
        }, keys);
        return keys.getKey().longValue();
    }

    public Map<String, Object> policy() { return jdbc.queryForMap("SELECT normal_limit_kph,crowd_limit_kph,crowd_person_count,confidence_threshold,alert_cooldown_ms FROM rg_policy WHERE id=1"); }

    public void policy(double normal, double crowd, int people) {
        if (!Double.isFinite(normal) || !Double.isFinite(crowd) || normal <= 0 || normal > 80 || crowd <= 0 || crowd >= normal || people < 1 || people > 50) bad("限速或人数阈值无效");
        jdbc.update("UPDATE rg_policy SET normal_limit_kph=?,crowd_limit_kph=?,crowd_person_count=? WHERE id=1", normal, crowd, people);
    }

    public List<Map<String, Object>> riders() { return jdbc.queryForList("SELECT id,name,phone,created_at FROM rg_rider ORDER BY id DESC LIMIT 200"); }
    public List<Map<String, Object>> devices() {
        return jdbc.queryForList("SELECT d.device_id,d.rider_id,r.name AS rider_name,d.last_seen_ms,d.last_sample_ms,d.last_lat,d.last_lng,d.last_speed,d.gps_valid,d.crowd_mode,d.last_image_ms," +
            " i.id AS latest_image_id,i.inference_status,i.inference_mode,i.person_count,i.captured_ms AS latest_image_captured_ms" +
            " FROM rg_device d LEFT JOIN rg_rider r ON r.id=d.rider_id" +
            " LEFT JOIN rg_image i ON i.id=(SELECT x.id FROM rg_image x WHERE x.device_id=d.device_id ORDER BY x.captured_ms DESC,x.id DESC LIMIT 1)" +
            " ORDER BY d.device_id LIMIT 500");
    }
    public List<Map<String, Object>> tracks(String deviceId, long fromMs, long toMs) {
        device(deviceId);
        return jdbc.queryForList("SELECT id,device_id,captured_ms,lat,lng,speed_kph,gps_valid FROM rg_track WHERE device_id=? AND captured_ms BETWEEN ? AND ? ORDER BY captured_ms LIMIT 5000", deviceId, fromMs, toMs);
    }
    public List<Map<String, Object>> events() {
        return jdbc.queryForList("SELECT e.id,e.device_id,r.name AS rider_name,e.image_id,i.inference_mode,e.event_type,e.crowd_mode,e.speed_kph,e.speed_limit_kph,e.lat,e.lng,e.captured_ms,e.status FROM rg_event e LEFT JOIN rg_device d ON d.device_id=e.device_id LEFT JOIN rg_rider r ON r.id=d.rider_id LEFT JOIN rg_image i ON i.id=e.image_id ORDER BY e.captured_ms DESC LIMIT 200");
    }
    public Map<String, Object> event(long id) {
        List<Map<String, Object>> rows = jdbc.queryForList("SELECT e.id,e.device_id,r.name AS rider_name,e.image_id,i.inference_mode,i.person_count,e.event_type,e.crowd_mode,e.speed_kph,e.speed_limit_kph,e.lat,e.lng,e.captured_ms,e.status" +
            " FROM rg_event e LEFT JOIN rg_device d ON d.device_id=e.device_id LEFT JOIN rg_rider r ON r.id=d.rider_id LEFT JOIN rg_image i ON i.id=e.image_id WHERE e.id=?", id);
        if (rows.isEmpty()) throw new ResponseStatusException(HttpStatus.NOT_FOUND, "事件不存在");
        Map<String, Object> result = new HashMap<>(rows.getFirst());
        result.put("actions", jdbc.queryForList("SELECT actor_user_id,actor_name,old_status,new_status,note,created_at FROM rg_event_action WHERE event_id=? ORDER BY id", id));
        return result;
    }
    @Transactional
    public void processEvent(long id, String status, String note, long actorUserId, String actorName) {
        if (!Set.of("REVIEWING", "RESOLVED", "DISMISSED").contains(status)) bad("无效的处置状态");
        if (note == null || note.isBlank() || note.strip().length() > 500) bad("请填写 1～500 字的处置说明");
        List<String> current = jdbc.query("SELECT status FROM rg_event WHERE id=? FOR UPDATE", (rs, rowNum) -> rs.getString(1), id);
        if (current.isEmpty()) throw new ResponseStatusException(HttpStatus.NOT_FOUND, "事件不存在");
        String oldStatus = current.getFirst();
        if (!"OPEN".equals(oldStatus) && !"REVIEWING".equals(oldStatus)) bad("事件已完成处置");
        jdbc.update("UPDATE rg_event SET status=? WHERE id=?", status, id);
        jdbc.update("INSERT INTO rg_event_action (event_id,actor_user_id,actor_name,old_status,new_status,note) VALUES (?,?,?,?,?,?)",
            id, actorUserId, actorName, oldStatus, status, note.strip());
    }
    public Map<String, Object> aiHealth() {
        long since = System.currentTimeMillis() - 3_600_000L;
        long ready = jdbc.queryForObject("SELECT COUNT(*) FROM rg_inference_attempt WHERE result='READY' AND created_at>=FROM_UNIXTIME(?)", Long.class, since / 1000);
        long failed = jdbc.queryForObject("SELECT COUNT(*) FROM rg_inference_attempt WHERE result='ERROR' AND created_at>=FROM_UNIXTIME(?)", Long.class, since / 1000);
        Double averageMs = jdbc.queryForObject("SELECT COALESCE(AVG(duration_ms),0) FROM rg_inference_attempt WHERE result='READY' AND created_at>=FROM_UNIXTIME(?)",
            Double.class, since / 1000);
        Map<String, Object> result = new HashMap<>();
        result.put("readyLastHour", ready);
        result.put("failedLastHour", failed);
        result.put("averageInferenceMs", Math.round(averageMs == null ? 0 : averageMs));
        result.put("checkedAtMs", System.currentTimeMillis());
        try {
            HttpRequest request = HttpRequest.newBuilder(URI.create(aiUrl + "/health")).timeout(Duration.ofSeconds(3)).GET().build();
            HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() != 200) throw new IOException("AI health check failed");
            JsonNode payload = mapper.readTree(response.body());
            result.put("status", "UP");
            result.put("mode", payload.path("mode").asText("unknown"));
        } catch (IOException | InterruptedException | IllegalArgumentException error) {
            if (error instanceof InterruptedException) Thread.currentThread().interrupt();
            result.put("status", "DOWN");
            result.put("mode", "unknown");
        }
        return result;
    }
    public Map<String, Object> analytics() {
        long since = System.currentTimeMillis() - 7L * 86_400_000L;
        List<Map<String, Object>> daily = jdbc.queryForList("SELECT DATE(FROM_UNIXTIME(captured_ms/1000)) AS day,COUNT(*) AS events," +
            " SUM(status='OPEN' OR status='REVIEWING') AS pending FROM rg_event WHERE captured_ms>=? GROUP BY day ORDER BY day", since);
        return Map.of("dailyEvents", daily);
    }
    public byte[] image(long id) {
        List<String> names = jdbc.query("SELECT file_name FROM rg_image WHERE id=?", (rs, i) -> rs.getString(1), id);
        if (names.isEmpty()) throw new ResponseStatusException(HttpStatus.NOT_FOUND, "图片不存在");
        try { return Files.readAllBytes(Path.of(imageDirectory).toAbsolutePath().normalize().resolve(names.getFirst())); }
        catch (IOException error) { throw new ResponseStatusException(HttpStatus.NOT_FOUND, "图片文件不存在", error); }
    }
    public Map<String, Object> overview() {
        long online = jdbc.queryForObject("SELECT COUNT(*) FROM rg_device WHERE last_seen_ms>=?", Long.class, System.currentTimeMillis() - 15000);
        long todayEvents = jdbc.queryForObject("SELECT COUNT(*) FROM rg_event WHERE captured_ms>=?", Long.class, System.currentTimeMillis() - 86_400_000L);
        long open = jdbc.queryForObject("SELECT COUNT(*) FROM rg_event WHERE status IN ('OPEN','REVIEWING')", Long.class);
        long riders = jdbc.queryForObject("SELECT COUNT(*) FROM rg_rider", Long.class);
        return Map.of("onlineDevices", online, "riders", riders, "todayEvents", todayEvents, "openEvents", open);
    }

    private Map<String, Object> device(String id) {
        List<Map<String, Object>> rows = jdbc.queryForList("SELECT * FROM rg_device WHERE device_id=?", id);
        if (rows.isEmpty()) throw new ResponseStatusException(HttpStatus.NOT_FOUND, "设备不存在");
        return rows.getFirst();
    }
    private void authorize(String id, String token) {
        if (id == null || token == null || !token.matches("[0-9a-f]{64}")) throw new ResponseStatusException(HttpStatus.UNAUTHORIZED, "设备凭证无效");
        List<String> hashes = jdbc.query("SELECT token_hash FROM rg_device WHERE device_id=?", (rs, i) -> rs.getString(1), id);
        if (hashes.isEmpty() || !MessageDigest.isEqual(hashes.getFirst().getBytes(StandardCharsets.US_ASCII), sha256(token).getBytes(StandardCharsets.US_ASCII)))
            throw new ResponseStatusException(HttpStatus.UNAUTHORIZED, "设备凭证无效");
    }
    private String sha256(String value) {
        try { return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(value.getBytes(StandardCharsets.UTF_8))); }
        catch (Exception error) { throw new IllegalStateException(error); }
    }
    private static long number(Object value, long fallback) { return value instanceof Number n ? n.longValue() : fallback; }
    private static double number(Object value, double fallback) { return value instanceof Number n ? n.doubleValue() : fallback; }
    private static void bad(String message) { throw new ResponseStatusException(HttpStatus.BAD_REQUEST, message); }
}
