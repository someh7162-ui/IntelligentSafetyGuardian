package org.dromara.web.riderguard;

import cn.dev33.satoken.annotation.SaCheckRole;
import cn.dev33.satoken.annotation.SaMode;
import lombok.RequiredArgsConstructor;
import org.dromara.common.core.domain.R;
import org.dromara.common.satoken.utils.LoginHelper;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.server.ResponseStatusException;

import java.util.List;
import java.util.Map;

/** Protected by the existing RuoYi login interceptor. */
@RestController
@RequestMapping("/riderguard")
@RequiredArgsConstructor
@SaCheckRole(value = {"superadmin", "riderguard_manager", "riderguard_viewer"}, mode = SaMode.OR)
public class RiderGuardAdminController {
    private final RiderGuardService service;

    public record RiderInput(String name, String phone) {}
    public record DeviceInput(String deviceId, Long riderId) {}
    public record BindInput(Long riderId) {}
    public record PolicyInput(double normalLimitKph, double crowdLimitKph, int crowdPersonCount) {}
    public record EventActionInput(String status, String note) {}

    @GetMapping("/overview")
    public R<Map<String, Object>> overview() { return R.ok(service.overview()); }
    @GetMapping("/riders")
    public R<List<Map<String, Object>>> riders() { return R.ok(service.riders()); }
    @PostMapping("/riders")
    @SaCheckRole(value = {"superadmin", "riderguard_manager"}, mode = SaMode.OR)
    public R<Map<String, Object>> addRider(@RequestBody RiderInput input) { return R.ok(Map.of("id", service.addRider(input.name(), input.phone()))); }
    @GetMapping("/devices")
    public R<List<Map<String, Object>>> devices() { return R.ok(service.devices()); }
    @PostMapping("/devices")
    @SaCheckRole(value = {"superadmin", "riderguard_manager"}, mode = SaMode.OR)
    public R<Map<String, Object>> provision(@RequestBody DeviceInput input) { return R.ok(service.provision(input.deviceId(), input.riderId())); }
    @PostMapping("/devices/{id}/rotate-token")
    @SaCheckRole(value = {"superadmin", "riderguard_manager"}, mode = SaMode.OR)
    public R<Map<String, Object>> rotate(@PathVariable String id) { return R.ok(service.rotateToken(id)); }
    @PutMapping("/devices/{id}/rider")
    @SaCheckRole(value = {"superadmin", "riderguard_manager"}, mode = SaMode.OR)
    public R<Void> bind(@PathVariable String id, @RequestBody BindInput input) { service.bind(id, input.riderId()); return R.ok(); }
    @GetMapping("/devices/{id}/tracks")
    public R<List<Map<String, Object>>> tracks(@PathVariable String id, @RequestParam long fromMs, @RequestParam long toMs) {
        return R.ok(service.tracks(id, fromMs, toMs));
    }
    @GetMapping("/events")
    public R<List<Map<String, Object>>> events() { return R.ok(service.events()); }
    @GetMapping("/events/{id}")
    public R<Map<String, Object>> event(@PathVariable long id) { return R.ok(service.event(id)); }
    @PutMapping("/events/{id}/process")
    @SaCheckRole(value = {"superadmin", "riderguard_manager"}, mode = SaMode.OR)
    public R<Void> process(@PathVariable long id, @RequestBody EventActionInput input) {
        service.processEvent(id, input.status(), input.note(), LoginHelper.getUserId(), LoginHelper.getUsername());
        return R.ok();
    }
    @PutMapping("/events/{id}/resolve")
    @SaCheckRole(value = {"superadmin", "riderguard_manager"}, mode = SaMode.OR)
    public R<Void> resolve(@PathVariable long id) {
        service.processEvent(id, "RESOLVED", "从总览页快速处置", LoginHelper.getUserId(), LoginHelper.getUsername());
        return R.ok();
    }
    @GetMapping(value = "/images/{id}", produces = MediaType.IMAGE_JPEG_VALUE)
    public ResponseEntity<byte[]> image(@PathVariable long id) {
        return ResponseEntity.ok().contentType(MediaType.IMAGE_JPEG).body(service.image(id));
    }
    @GetMapping("/policy")
    public R<Map<String, Object>> policy() { return R.ok(service.policy()); }
    @PutMapping("/policy")
    @SaCheckRole(value = {"superadmin", "riderguard_manager"}, mode = SaMode.OR)
    public R<Void> policy(@RequestBody PolicyInput input) {
        service.policy(input.normalLimitKph(), input.crowdLimitKph(), input.crowdPersonCount());
        return R.ok();
    }
    @GetMapping("/ai/health")
    public R<Map<String, Object>> aiHealth() { return R.ok(service.aiHealth()); }
    @GetMapping("/analytics")
    public R<Map<String, Object>> analytics() { return R.ok(service.analytics()); }

    @ExceptionHandler(ResponseStatusException.class)
    public ResponseEntity<R<Void>> rejected(ResponseStatusException error) {
        return ResponseEntity.status(error.getStatusCode()).body(R.fail(error.getStatusCode().value(), error.getReason()));
    }
}
