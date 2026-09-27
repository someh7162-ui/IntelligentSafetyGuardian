package org.dromara.web.riderguard;

import cn.dev33.satoken.annotation.SaIgnore;
import lombok.RequiredArgsConstructor;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;
import org.springframework.web.server.ResponseStatusException;

import java.io.IOException;
import java.util.Map;

/** Public transport endpoints; each call authenticates its own device token. */
@SaIgnore
@RestController
@RequestMapping("/device/riderguard")
@RequiredArgsConstructor
public class RiderGuardDeviceController {
    private final RiderGuardService service;

    @PostMapping("/telemetry")
    public Map<String, Object> telemetry(@RequestHeader("X-Device-Token") String token,
                                          @RequestBody RiderGuardService.Telemetry telemetry) {
        return service.telemetry(telemetry, token);
    }

    @PostMapping("/image")
    public Map<String, Object> image(@RequestHeader("X-Device-Token") String token,
                                     @RequestParam String deviceId, @RequestParam String sampleId,
                                     @RequestParam long capturedAtMs, @RequestParam MultipartFile image,
                                     @RequestHeader(value = "X-Demo-People", required = false) Integer demoPeople) throws IOException {
        return service.image(deviceId, sampleId, capturedAtMs, image.getBytes(), token, demoPeople);
    }

    @ExceptionHandler(ResponseStatusException.class)
    public ResponseEntity<Map<String, String>> rejected(ResponseStatusException error) {
        return ResponseEntity.status(error.getStatusCode()).body(Map.of("error", error.getReason() == null ? "请求无效" : error.getReason()));
    }
}
