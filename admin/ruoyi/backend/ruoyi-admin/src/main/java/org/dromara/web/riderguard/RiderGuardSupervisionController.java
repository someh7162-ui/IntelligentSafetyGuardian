package org.dromara.web.riderguard;
import cn.dev33.satoken.annotation.*;
import lombok.RequiredArgsConstructor;
import org.dromara.common.core.domain.R;
import org.dromara.common.satoken.utils.LoginHelper;
import org.springframework.http.*;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.server.ResponseStatusException;
import java.util.*;

@RestController
@RequestMapping("/riderguard/supervision")
@RequiredArgsConstructor
@SaCheckRole(value={"superadmin","riderguard_manager","riderguard_viewer"},mode=SaMode.OR)
public class RiderGuardSupervisionController {
    private final RiderGuardSupervisionService service;
    @GetMapping("/policy") public R<RiderGuardSupervisionService.Policy> policy(){return R.ok(service.policy());}
    @PutMapping("/policy") @SaCheckRole(value={"superadmin","riderguard_manager"},mode=SaMode.OR)
    public R<RiderGuardSupervisionService.Policy> policy(@RequestBody RiderGuardSupervisionService.Policy p){return R.ok(service.savePolicy(p));}
    @GetMapping("/intersections") public R<List<RiderGuardSupervisionService.Road>> roads(){return R.ok(service.roads());}
    @PutMapping("/intersections") @SaCheckRole(value={"superadmin","riderguard_manager"},mode=SaMode.OR)
    public R<List<RiderGuardSupervisionService.Road>> roads(@RequestBody List<RiderGuardSupervisionService.Road> r){return R.ok(service.saveRoads(r));}
    @GetMapping("/events") public R<List<Map<String,Object>>> events(@RequestParam(defaultValue="false") boolean mock){return R.ok(service.events(mock));}
    @GetMapping("/events/{id}") public R<Map<String,Object>> event(@PathVariable long id){return R.ok(service.detail(id));}
    @PutMapping("/events/{id}/review") @SaCheckRole(value={"superadmin","riderguard_manager"},mode=SaMode.OR)
    public R<Void> review(@PathVariable long id,@RequestBody RiderGuardSupervisionService.Review r){service.review(id,r,LoginHelper.getUserId(),LoginHelper.getUsername());return R.ok();}
    @GetMapping(value="/images/{id}",produces=MediaType.IMAGE_JPEG_VALUE)
    public ResponseEntity<byte[]> image(@PathVariable long id){return ResponseEntity.ok().contentType(MediaType.IMAGE_JPEG).body(service.photo(id));}
    @ExceptionHandler(ResponseStatusException.class) public ResponseEntity<R<Void>> error(ResponseStatusException e){return ResponseEntity.status(e.getStatusCode()).body(R.fail(e.getStatusCode().value(),e.getReason()));}
}
