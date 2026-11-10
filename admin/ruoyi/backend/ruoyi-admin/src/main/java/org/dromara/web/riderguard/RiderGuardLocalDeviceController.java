package org.dromara.web.riderguard;

import cn.dev33.satoken.annotation.SaCheckRole;
import cn.dev33.satoken.annotation.SaMode;
import jakarta.servlet.http.HttpServletRequest;
import lombok.RequiredArgsConstructor;
import org.dromara.common.core.domain.R;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.server.ResponseStatusException;

import java.net.InetAddress;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/riderguard/local-device")
@RequiredArgsConstructor
@SaCheckRole(value = {"superadmin", "riderguard_manager"}, mode = SaMode.OR)
public class RiderGuardLocalDeviceController {
    private final RiderGuardLocalDeviceService local;
    private final RiderGuardService devices;
    public record Start(String deviceId, String port) {}
    public record Action(String action, Boolean enabled) {}

    private void requireLocal(HttpServletRequest request) {
        try { if (InetAddress.getByName(request.getRemoteAddr()).isLoopbackAddress()) return; }
        catch (Exception ignored) { /* Fail closed. */ }
        throw new ResponseStatusException(HttpStatus.FORBIDDEN, "USB 测试仅限本机管理服务");
    }

    @GetMapping("/status")
    public R<Map<String, Object>> status(HttpServletRequest request) { requireLocal(request); return R.ok(local.status()); }
    @GetMapping("/ports")
    public R<List<Map<String, Object>>> ports(HttpServletRequest request) { requireLocal(request); return R.ok(local.ports()); }
    @PostMapping("/start")
    public R<Map<String, Object>> start(@RequestBody Start input, @RequestHeader(value = "X-Device-Token", required = false) String token, HttpServletRequest request) {
        requireLocal(request); devices.authorize(input.deviceId(), token);
        return R.ok(local.start(input.deviceId(), input.port(), token));
    }
    @PostMapping("/command")
    public R<Map<String, Object>> command(@RequestBody Action action, HttpServletRequest request) {
        requireLocal(request); return R.ok(local.command(action.action(), action.enabled()));
    }
    @PostMapping("/stop")
    public R<Map<String, Object>> stop(HttpServletRequest request) { requireLocal(request); return R.ok(local.stop()); }

    @ExceptionHandler(ResponseStatusException.class)
    public ResponseEntity<R<Void>> rejected(ResponseStatusException error) {
        return ResponseEntity.status(error.getStatusCode()).body(R.fail(error.getStatusCode().value(), error.getReason()));
    }
}
