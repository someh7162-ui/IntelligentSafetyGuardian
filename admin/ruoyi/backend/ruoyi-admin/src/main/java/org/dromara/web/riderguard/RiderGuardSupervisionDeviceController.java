package org.dromara.web.riderguard;
import cn.dev33.satoken.annotation.SaIgnore;
import lombok.RequiredArgsConstructor;
import org.springframework.http.*;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.server.ResponseStatusException;
import java.util.Map;

@SaIgnore @RestController @RequiredArgsConstructor
@RequestMapping("/device/riderguard")
public class RiderGuardSupervisionDeviceController {
    private final RiderGuardService devices;
    private final RiderGuardSupervisionService service;
    @GetMapping("/supervision-policy")
    public Map<String,Object> policy(@RequestParam String deviceId,@RequestHeader(value="X-Device-Token",required=false) String token){devices.authorize(deviceId,token);return service.devicePolicy();}
    @PostMapping("/events")
    public Map<String,Object> events(@RequestBody RiderGuardSupervisionService.Batch b,@RequestHeader(value="X-Device-Token",required=false) String token){devices.authorize(b.deviceId(),token);return service.ingest(b);}
    @PostMapping(value="/event-image",consumes=MediaType.IMAGE_JPEG_VALUE)
    public Map<String,Object> photo(@RequestParam String deviceId,@RequestParam String eventId,@RequestParam String photoId,
        @RequestParam(required=false) Long capturedAtMs,@RequestHeader(value="X-Device-Token",required=false) String token,@RequestBody byte[] jpeg){
        devices.authorize(deviceId,token);return service.savePhoto(deviceId,eventId,photoId,capturedAtMs,jpeg);
    }
    @ExceptionHandler(ResponseStatusException.class) public ResponseEntity<Map<String,String>> error(ResponseStatusException e){return ResponseEntity.status(e.getStatusCode()).body(Map.of("error",e.getReason()==null?"请求无效":e.getReason()));}
}
