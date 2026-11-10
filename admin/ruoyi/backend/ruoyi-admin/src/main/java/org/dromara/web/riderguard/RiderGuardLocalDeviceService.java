package org.dromara.web.riderguard;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.annotation.PreDestroy;
import lombok.RequiredArgsConstructor;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.web.server.ResponseStatusException;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;
import java.util.concurrent.TimeUnit;

/** Explicitly enabled, authenticated Windows-local USB worker. No remote command execution API. */
@Service
@RequiredArgsConstructor
public class RiderGuardLocalDeviceService {
    private final RiderGuardSampling sampling;
    private final ObjectMapper mapper = new ObjectMapper();
    @Value("${RIDERGUARD_LOCAL_TEST_ENABLED:false}") private boolean enabled;
    @Value("${RIDERGUARD_GATEWAY_PYTHON:}") private String python;
    @Value("${RIDERGUARD_USB_GATEWAY_SCRIPT:}") private String script;
    @Value("${server.port:8080}") private int backendPort;
    private Process worker;
    private BufferedWriter input;
    private String deviceId = "", port = "";
    private boolean connected, autoImages;
    private long startedAtMs, eventNumber;
    private Map<String, Object> telemetry = Map.of(), policy = Map.of(), image = Map.of();
    private final ArrayDeque<Map<String, Object>> events = new ArrayDeque<>();

    private boolean available() {
        return enabled && System.getProperty("os.name", "").startsWith("Windows")
            && !python.isBlank() && !script.isBlank() && Files.isRegularFile(Path.of(python)) && Files.isRegularFile(Path.of(script));
    }

    private void requireAvailable() {
        if (!available()) throw new ResponseStatusException(HttpStatus.SERVICE_UNAVAILABLE, "本机 USB 测试未启用，请使用项目本机启动脚本");
    }

    public synchronized Map<String, Object> status() {
        Map<String, Object> state = new LinkedHashMap<>();
        state.put("available", available());
        state.put("running", worker != null && worker.isAlive());
        state.put("connected", connected && worker != null && worker.isAlive());
        state.put("deviceId", deviceId); state.put("port", port); state.put("autoImages", autoImages);
        state.put("startedAtMs", startedAtMs); state.put("telemetry", telemetry); state.put("policy", policy);
        state.put("image", image); state.put("events", List.copyOf(events));
        state.put("telemetryIntervalMs", sampling.telemetryIntervalMs());
        state.put("imageIntervalMs", sampling.imageIntervalMs());
        state.put("visionStaleAfterMs", sampling.visionStaleAfterMs());
        return state;
    }

    public List<Map<String, Object>> ports() {
        requireAvailable();
        Process process = null;
        try {
            process = new ProcessBuilder(python, "-u", script, "--list-ports").redirectErrorStream(true).start();
            if (!process.waitFor(5, TimeUnit.SECONDS)) throw new IOException("Timed out");
            byte[] output = process.getInputStream().readNBytes(16385);
            if (process.exitValue() != 0 || output.length > 16384) throw new IOException("Invalid port list");
            return mapper.readValue(output, new TypeReference<>() {});
        } catch (IOException error) {
            throw new ResponseStatusException(HttpStatus.SERVICE_UNAVAILABLE, "无法枚举串口，请检查本机 USB 网关依赖");
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new ResponseStatusException(HttpStatus.SERVICE_UNAVAILABLE, "串口枚举已取消");
        } finally { if (process != null && process.isAlive()) process.destroyForcibly(); }
    }

    public synchronized Map<String, Object> start(String id, String selectedPort, String token) {
        requireAvailable();
        if (worker != null && worker.isAlive()) throw new ResponseStatusException(HttpStatus.CONFLICT, "已有 USB 测试运行，请先停止");
        if (id == null || !id.matches("[A-Za-z0-9_-]{3,64}") || selectedPort == null || !selectedPort.matches("COM[1-9]\\d{0,2}")
            || token == null || !token.matches("[0-9a-f]{64}"))
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "设备编号、串口或设备凭证无效");
        try {
            ProcessBuilder builder = new ProcessBuilder(python, "-u", script, "--controlled", "--device-id", id,
                "--port", selectedPort, "--base-url", "http://127.0.0.1:" + backendPort,
                "--telemetry-ms", String.valueOf(sampling.telemetryIntervalMs()), "--image-ms", String.valueOf(sampling.imageIntervalMs()));
            builder.environment().put("RIDERGUARD_DEVICE_TOKEN", token);
            builder.environment().put("PYTHONIOENCODING", "utf-8");
            builder.redirectErrorStream(true);
            worker = builder.start();
            input = new BufferedWriter(new OutputStreamWriter(worker.getOutputStream(), StandardCharsets.UTF_8));
            deviceId = id; port = selectedPort; connected = false; autoImages = false;
            startedAtMs = System.currentTimeMillis(); telemetry = Map.of(); policy = Map.of(); image = Map.of(); events.clear();
            Process session = worker;
            Thread.ofVirtual().name("riderguard-usb-events").start(() -> readEvents(session));
            return status();
        } catch (IOException error) {
            throw new ResponseStatusException(HttpStatus.SERVICE_UNAVAILABLE, "无法启动本机 USB 网关");
        }
    }

    public synchronized Map<String, Object> command(String action, Boolean imagesEnabled) {
        if (action == null || !Set.of("capture", "images").contains(action)) throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "无效控制指令");
        if (worker == null || !worker.isAlive() || input == null)
            throw new ResponseStatusException(HttpStatus.CONFLICT, "请先开始 USB 测试");
        try {
            input.write(mapper.writeValueAsString(Map.of("action", action, "enabled", Boolean.TRUE.equals(imagesEnabled))));
            input.newLine(); input.flush();
            return status();
        } catch (IOException error) { throw new ResponseStatusException(HttpStatus.CONFLICT, "USB 网关已退出，请重新开始"); }
    }

    public Map<String, Object> stop() {
        Process session;
        synchronized (this) {
            session = worker;
            if (session == null) return status();
            try { if (input != null) { input.write("{\"action\":\"stop\"}\n"); input.flush(); } }
            catch (IOException ignored) { /* Process exit already closes the worker. */ }
        }
        try { if (!session.waitFor(4, TimeUnit.SECONDS)) session.destroyForcibly(); }
        catch (InterruptedException error) { Thread.currentThread().interrupt(); session.destroyForcibly(); }
        synchronized (this) { if (worker == session) { connected = false; autoImages = false; } return status(); }
    }

    private void readEvents(Process session) {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(session.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.length() > 8192) continue;
                try {
                    Map<String, Object> event = mapper.readValue(line.replaceAll("[0-9a-fA-F]{64}", "[redacted]"), new TypeReference<>() {});
                    synchronized (this) {
                        if (worker != session) return;
                        event.put("id", ++eventNumber);
                        if ("started".equals(event.get("kind"))) connected = true;
                        if ("stopped".equals(event.get("kind"))) { connected = false; autoImages = false; }
                        if (event.get("autoImages") instanceof Boolean value) autoImages = value;
                        if (event.get("telemetry") instanceof Map<?, ?> value) telemetry = stringMap(value);
                        if (event.get("policy") instanceof Map<?, ?> value) policy = stringMap(value);
                        if (event.get("result") instanceof Map<?, ?> value) image = stringMap(value);
                        events.addFirst(event);
                        while (events.size() > 40) events.removeLast();
                    }
                } catch (IOException ignored) { /* Ignore non-protocol output without exposing process/environment data. */ }
            }
        } catch (IOException ignored) { /* Expected when the worker is stopped. */ }
        finally { synchronized (this) { if (worker == session) { connected = false; autoImages = false; } } }
    }

    private static Map<String, Object> stringMap(Map<?, ?> value) {
        Map<String, Object> result = new LinkedHashMap<>();
        value.forEach((key, entry) -> { if (key instanceof String text) result.put(text, entry); });
        return result;
    }

    @PreDestroy
    void shutdown() { stop(); }
}
