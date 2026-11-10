package org.dromara.web.riderguard;

import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.annotation.PostConstruct;
import lombok.RequiredArgsConstructor;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.dao.DuplicateKeyException;
import org.springframework.http.HttpStatus;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.server.ResponseStatusException;

import javax.imageio.ImageIO;
import java.io.ByteArrayInputStream;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;

/** Device-originated safety evidence. No automatic violation decision or inference. */
@Service
@RequiredArgsConstructor
public class RiderGuardSupervisionService {
    private final JdbcTemplate jdbc;
    private final ObjectMapper json = new ObjectMapper();
    @Value("${RIDERGUARD_IMAGE_DIR:deploy/local-runtime/uploads}")
    private String imageDirectory = "deploy/local-runtime/uploads";

    public record Policy(long version, boolean enabled, Double speedLimitKph, int speedingHoldMs,
                         int recoveryMs, int windowMs, int countThreshold, double brakeThresholdMs2,
                         int brakeHoldMs, int forwardAxis, int gpsFreshMs, int telemetryIntervalMs,
                         int approachPhotos, int photoIntervalMs) {
        static Policy defaults() { return new Policy(1,true,null,3000,5000,600000,3,3.0,200,1,2500,3000,3,1100); }
    }
    public record Road(Long id, String name, double latitude, double longitude, int radiusM,
                       String movement, String signalGroup, boolean enabled) {}
    public record Trace(long offsetMs, double forwardMs2, double verticalMs2, double gyroDps) {}
    public record Event(String eventId, String type, long policyVersion, Long capturedAtMs,
                        long startUptimeMs, long qualifiedUptimeMs, long endUptimeMs,
                        boolean gpsValid, Double latitude, Double longitude, Long gpsAgeMs,
                        Double speedBeforeKph, Double speedAfterKph, Double peakSpeedKph,
                        Double limitKph, Double minForwardMs2, Long intersectionId,
                        boolean partial, boolean mock, List<Trace> trace) {}
    public record Batch(String deviceId, String batchId, List<Event> events) {}
    public record Review(String status, String note) {}

    @PostConstruct
    public void initialize() {
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_supervision_policy (
              version BIGINT PRIMARY KEY,config_json LONGTEXT NOT NULL,created_ms BIGINT NOT NULL)
            """
        );
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_supervision_road (
              id BIGINT AUTO_INCREMENT PRIMARY KEY,name VARCHAR(100) NOT NULL,lat DOUBLE NOT NULL,lng DOUBLE NOT NULL,
              radius_m INT NOT NULL,movement VARCHAR(40) NOT NULL,signal_group VARCHAR(80),enabled BOOLEAN NOT NULL)
            """
        );
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_safety_event (
              id BIGINT AUTO_INCREMENT PRIMARY KEY,device_id VARCHAR(64) NOT NULL,event_id VARCHAR(80) NOT NULL,
              batch_id VARCHAR(80),event_type VARCHAR(32) NOT NULL,policy_version BIGINT NOT NULL,captured_ms BIGINT,
              received_ms BIGINT NOT NULL,gps_valid BOOLEAN NOT NULL,lat DOUBLE,lng DOUBLE,peak_speed DOUBLE,
              speed_limit DOUBLE,duration_ms BIGINT NOT NULL,min_forward DOUBLE,intersection_id BIGINT,
              is_mock BOOLEAN NOT NULL,partial_evidence BOOLEAN NOT NULL,payload_json LONGTEXT NOT NULL,
              payload_hash CHAR(64) NOT NULL,status VARCHAR(24) NOT NULL DEFAULT 'PENDING',
              UNIQUE (device_id,event_id))
            """
        );
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_safety_image (
              id BIGINT AUTO_INCREMENT PRIMARY KEY,device_id VARCHAR(64) NOT NULL,event_id VARCHAR(80) NOT NULL,
              photo_id VARCHAR(80) NOT NULL,captured_ms BIGINT,file_name VARCHAR(120) NOT NULL,content_hash CHAR(64) NOT NULL,
              UNIQUE(device_id,event_id,photo_id))
            """
        );
        jdbc.execute("""
            CREATE TABLE IF NOT EXISTS rg_safety_review (
              id BIGINT AUTO_INCREMENT PRIMARY KEY,event_db_id BIGINT NOT NULL,actor_id BIGINT NOT NULL,
              actor_name VARCHAR(100) NOT NULL,old_status VARCHAR(24) NOT NULL,new_status VARCHAR(24) NOT NULL,
              note VARCHAR(1000) NOT NULL,created_ms BIGINT NOT NULL)
            """
        );
        if(jdbc.queryForObject("SELECT COUNT(*) FROM rg_supervision_policy",Long.class)==0) {
            try { jdbc.update("INSERT INTO rg_supervision_policy VALUES (?,?,?)",1,encode(Policy.defaults()),System.currentTimeMillis()); }
            catch(DuplicateKeyException ignored) { /* Another starter initialized the same version. */ }
        }
    }
    public Policy policy() {
        String value=jdbc.queryForObject("SELECT config_json FROM rg_supervision_policy ORDER BY version DESC LIMIT 1",String.class);
        try { return json.readValue(value,Policy.class); } catch(Exception e) { throw new IllegalStateException("Invalid stored supervision policy",e); }
    }
    private Policy historical(long version) {
        List<String> rows=jdbc.query("SELECT config_json FROM rg_supervision_policy WHERE version=?",(r,i)->r.getString(1),version);
        if(rows.isEmpty())bad("策略版本不存在");
        try { return json.readValue(rows.getFirst(),Policy.class); }catch(Exception e){throw new IllegalStateException(e);}
    }
    @Transactional
    public synchronized Policy savePolicy(Policy p) {
        validatePolicy(p);
        long next=policy().version()+1;
        Policy saved=new Policy(next,p.enabled(),p.speedLimitKph(),p.speedingHoldMs(),p.recoveryMs(),p.windowMs(),
            p.countThreshold(),p.brakeThresholdMs2(),p.brakeHoldMs(),p.forwardAxis(),2500,3000,3,1100);
        jdbc.update("INSERT INTO rg_supervision_policy VALUES (?,?,?)",next,encode(saved),System.currentTimeMillis());
        return saved;
    }
    static void validatePolicy(Policy p) {
        if(p==null || p.speedLimitKph()!=null&&(!finite(p.speedLimitKph())||p.speedLimitKph()<=0||p.speedLimitKph()>80)
            ||p.speedingHoldMs()<1000||p.speedingHoldMs()>30000||p.recoveryMs()<1000||p.recoveryMs()>30000
            ||p.windowMs()<60000||p.windowMs()>3600000||p.countThreshold()<2||p.countThreshold()>32
            ||!finite(p.brakeThresholdMs2())||p.brakeThresholdMs2()<1||p.brakeThresholdMs2()>12
            ||p.brakeHoldMs()<100||p.brakeHoldMs()>2000||p.forwardAxis()==0||Math.abs(p.forwardAxis())>3)bad("监管策略参数无效");
    }
    public Map<String,Object> devicePolicy() { return Map.of("policy",policy(),"intersections",roads(),"eventImagesOnly",true); }
    public List<Road> roads() {
        return jdbc.query("SELECT * FROM rg_supervision_road ORDER BY id LIMIT 8",(r,i)->new Road(r.getLong("id"),r.getString("name"),
            r.getDouble("lat"),r.getDouble("lng"),r.getInt("radius_m"),r.getString("movement"),r.getString("signal_group"),r.getBoolean("enabled")));
    }
    @Transactional
    public synchronized List<Road> saveRoads(List<Road> input) {
        if(input==null||input.size()>8)bad("最多配置8个试验路口");
        Set<Long> ids=new HashSet<>();
        for(Road r:input) {
            if(r==null||r.id()==null||r.id()<1||r.id()>Integer.MAX_VALUE||!ids.add(r.id())||r.name()==null||r.name().isBlank()||r.name().length()>100
               ||!coordinate(r.latitude(),r.longitude())||r.radiusM()<10||r.radiusM()>300||r.movement()==null||r.movement().length()>40
               ||r.signalGroup()!=null&&r.signalGroup().length()>80)bad("路口配置无效");
        }
        /* Replace only this feature's configuration, never historical evidence. */
        jdbc.update("DELETE FROM rg_supervision_road");
        for(Road r:input)jdbc.update("INSERT INTO rg_supervision_road (id,name,lat,lng,radius_m,movement,signal_group,enabled) VALUES (?,?,?,?,?,?,?,?)",
            r.id(),r.name(),r.latitude(),r.longitude(),r.radiusM(),r.movement(),r.signalGroup(),r.enabled());
        return roads();
    }
    static void identifier(String value) { if(value==null||!value.matches("[A-Za-z0-9_-]{1,80}"))bad("无效事件编号"); }
    static boolean finite(Double n) { return n!=null&&Double.isFinite(n); }
    static boolean coordinate(Double lat,Double lng) { return finite(lat)&&finite(lng)&&Math.abs(lat)<=90&&Math.abs(lng)<=180; }
    static void validateEvent(Event e,Policy p,long now) {
        if(e==null)bad("缺少事件");identifier(e.eventId());
        if(e.type()==null||!Set.of("HARSH_BRAKE","OVERSPEED","INTERSECTION_REVIEW").contains(e.type()))bad("不支持的事件类型");
        if(e.capturedAtMs()!=null&&(e.capturedAtMs()<1_577_836_800_000L||e.capturedAtMs()>now+300000))bad("事件时间无效");
        if(e.startUptimeMs()<0||e.startUptimeMs()>0xffff_ffffL||e.endUptimeMs()<0||e.endUptimeMs()>0xffff_ffffL
            ||((e.endUptimeMs()-e.startUptimeMs())&0xffff_ffffL)>86_400_000L)bad("事件时长无效");
        if(e.gpsValid()&&(!coordinate(e.latitude(),e.longitude())||e.gpsAgeMs()==null||e.gpsAgeMs()<0||e.gpsAgeMs()>p.gpsFreshMs()))bad("GPS证据无效");
        if(!e.gpsValid()&&(e.latitude()!=null||e.longitude()!=null))bad("未定位事件不得携带有效位置");
        for(Double v:new Double[]{e.speedBeforeKph(),e.speedAfterKph(),e.peakSpeedKph()})if(v!=null&&(!finite(v)||v<0||v>150))bad("速度证据无效");
        if("OVERSPEED".equals(e.type())) {
            if(!e.gpsValid()||p.speedLimitKph()==null||!finite(e.limitKph())||Math.abs(e.limitKph()-p.speedLimitKph())>.001
                ||!finite(e.peakSpeedKph())||e.peakSpeedKph()<=e.limitKph()
                ||((e.qualifiedUptimeMs()-e.startUptimeMs())&0xffff_ffffL)<p.speedingHoldMs())bad("超速证据不满足策略");
        }
        if("HARSH_BRAKE".equals(e.type())&&(!finite(e.minForwardMs2())||e.minForwardMs2()>-p.brakeThresholdMs2()))bad("急刹触发依据无效");
        if("INTERSECTION_REVIEW".equals(e.type())&&(e.intersectionId()==null||!e.gpsValid()))bad("缺少路口或定位");
        if(e.trace()!=null) {
            if(e.trace().size()>40)bad("传感器片段过长");
            for(Trace t:e.trace())if(t==null||!finite(t.forwardMs2())||!finite(t.verticalMs2())||!finite(t.gyroDps())
                ||Math.abs(t.forwardMs2())>40||Math.abs(t.verticalMs2())>40||Math.abs(t.gyroDps())>500||Math.abs(t.offsetMs())>10000)bad("传感器片段无效");
        }
    }
    @Transactional
    public synchronized Map<String,Object> ingest(Batch b) {
        if(b==null||b.events()==null||b.events().isEmpty()||b.events().size()>32)bad("事件批次无效");
        identifier(b.batchId());
        List<String> ack=new ArrayList<>();Set<String> seen=new HashSet<>();
        for(Event e:b.events()) {
            if(e==null)bad("缺少事件内容");
            Policy p=historical(e.policyVersion());validateEvent(e,p,System.currentTimeMillis());
            if(!seen.add(e.eventId()))bad("批次包含重复事件");
            String payload=encode(e),hash=hash(payload.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            List<String> old=jdbc.query("SELECT payload_hash FROM rg_safety_event WHERE device_id=? AND event_id=?",(r,i)->r.getString(1),b.deviceId(),e.eventId());
            if(!old.isEmpty()) {
                if(!old.getFirst().equals(hash))throw new ResponseStatusException(HttpStatus.CONFLICT,"同一事件编号的内容发生变化");
            } else {
                jdbc.update("INSERT INTO rg_safety_event (device_id,event_id,batch_id,event_type,policy_version,captured_ms,received_ms,gps_valid,lat,lng,peak_speed,speed_limit,duration_ms,min_forward,intersection_id,is_mock,partial_evidence,payload_json,payload_hash,status) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,'PENDING')",
                    b.deviceId(),e.eventId(),b.batchId(),e.type(),e.policyVersion(),e.capturedAtMs(),System.currentTimeMillis(),e.gpsValid(),e.latitude(),e.longitude(),
                    e.peakSpeedKph(),e.limitKph(),(e.endUptimeMs()-e.startUptimeMs())&0xffff_ffffL,e.minForwardMs2(),e.intersectionId(),e.mock(),e.partial(),payload,hash);
            }
            ack.add(e.eventId());
        }
        return Map.of("status","SAVED","acknowledgedEventIds",ack,"batchId",b.batchId());
    }
    public synchronized Map<String,Object> savePhoto(String device,String event,String photo,Long captured,byte[] data) {
        identifier(event);identifier(photo);
        if(data==null||data.length<4||data.length>65536||data[0]!=(byte)255||data[1]!=(byte)216
            ||data[data.length-2]!=(byte)255||data[data.length-1]!=(byte)217)bad("必须上传不超过64KiB的完整JPEG");
        if(captured!=null&&(captured<1_577_836_800_000L||captured>System.currentTimeMillis()+300000))bad("图片时间无效");
        requireEvent(device,event);
        String digest=hash(data);
        List<Map<String,Object>> old=jdbc.queryForList("SELECT id,content_hash FROM rg_safety_image WHERE device_id=? AND event_id=? AND photo_id=?",device,event,photo);
        if(!old.isEmpty()) {
            if(!digest.equals(old.getFirst().get("content_hash")))throw new ResponseStatusException(HttpStatus.CONFLICT,"图片编号内容不一致");
            return Map.of("status","SAVED","photoId",photo);
        }
        if(jdbc.queryForObject("SELECT COUNT(*) FROM rg_safety_image WHERE device_id=? AND event_id=?",Integer.class,device,event)>=8)bad("单事件最多8张图片");
        try {
            var image=ImageIO.read(new ByteArrayInputStream(data));
            if(image==null||image.getWidth()>2048||image.getHeight()>2048||image.getWidth()<1||image.getHeight()<1)bad("JPEG不可解码或尺寸超限");
            Path dir=Path.of(imageDirectory).toAbsolutePath().normalize().resolve("supervision");Files.createDirectories(dir);
            String name=hash((device+"/"+event+"/"+photo).getBytes(java.nio.charset.StandardCharsets.UTF_8))+".jpg";
            Path target=dir.resolve(name),temp=Files.createTempFile(dir,"incoming-",".tmp");
            try {Files.write(temp,data);try{Files.move(temp,target,StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);}
                catch(AtomicMoveNotSupportedException ex){Files.move(temp,target,StandardCopyOption.REPLACE_EXISTING);}}
            finally{Files.deleteIfExists(temp);}
            jdbc.update("INSERT INTO rg_safety_image (device_id,event_id,photo_id,captured_ms,file_name,content_hash) VALUES (?,?,?,?,?,?)",device,event,photo,captured,name,digest);
            return Map.of("status","SAVED","photoId",photo);
        }catch(ResponseStatusException ex){throw ex;}catch(Exception ex){throw new ResponseStatusException(HttpStatus.INTERNAL_SERVER_ERROR,"证据图片保存失败",ex);}
    }
    private void requireEvent(String device,String event) {
        if(jdbc.queryForObject("SELECT COUNT(*) FROM rg_safety_event WHERE device_id=? AND event_id=?",Integer.class,device,event)==0)
            throw new ResponseStatusException(HttpStatus.NOT_FOUND,"事件尚未保存，请先补传事件摘要");
    }
    public List<Map<String,Object>> events(boolean mock) {
        return jdbc.queryForList("SELECT e.*,r.name AS rider_name,(SELECT COUNT(*) FROM rg_safety_image i WHERE i.device_id=e.device_id AND i.event_id=e.event_id) AS photo_count FROM rg_safety_event e LEFT JOIN rg_device d ON d.device_id=e.device_id LEFT JOIN rg_rider r ON r.id=d.rider_id WHERE e.is_mock=? ORDER BY e.received_ms DESC,e.id DESC LIMIT 200",mock);
    }
    public Map<String,Object> detail(long id) {
        List<Map<String,Object>> rows=jdbc.queryForList("SELECT * FROM rg_safety_event WHERE id=?",id);
        if(rows.isEmpty())throw new ResponseStatusException(HttpStatus.NOT_FOUND,"事件不存在");
        Map<String,Object> result=new HashMap<>(rows.getFirst());
        result.put("photos",jdbc.queryForList("SELECT id,photo_id,captured_ms,content_hash FROM rg_safety_image WHERE device_id=? AND event_id=? ORDER BY id",result.get("device_id"),result.get("event_id")));
        result.put("reviews",jdbc.queryForList("SELECT actor_name,old_status,new_status,note,created_ms FROM rg_safety_review WHERE event_db_id=? ORDER BY id",id));
        return result;
    }
    public byte[] photo(long id) {
        List<String> names=jdbc.query("SELECT file_name FROM rg_safety_image WHERE id=?",(r,i)->r.getString(1),id);
        if(names.isEmpty())throw new ResponseStatusException(HttpStatus.NOT_FOUND,"图片不存在");
        try{return Files.readAllBytes(Path.of(imageDirectory).toAbsolutePath().normalize().resolve("supervision").resolve(names.getFirst()));}
        catch(Exception ex){throw new ResponseStatusException(HttpStatus.NOT_FOUND,"图片文件不可用");}
    }
    @Transactional
    public synchronized void review(long id,Review value,long actor,String actorName) {
        if(value==null||value.status()==null||!Set.of("CONFIRMED","DISMISSED","INSUFFICIENT").contains(value.status())||value.note()==null||value.note().isBlank()||value.note().length()>1000)bad("请填写有效复核结果与说明");
        Map<String,Object> row=detail(id);
        if("CONFIRMED".equals(value.status())&&"INTERSECTION_REVIEW".equals(row.get("event_type"))
            &&((List<?>)row.get("photos")).size()<2)bad("路口事件至少需要两张连续现场图片；证据不足时请选择证据不足");
        jdbc.update("UPDATE rg_safety_event SET status=? WHERE id=?",value.status(),id);
        jdbc.update("INSERT INTO rg_safety_review (event_db_id,actor_id,actor_name,old_status,new_status,note,created_ms) VALUES (?,?,?,?,?,?,?)",id,actor,actorName,row.get("status"),value.status(),value.note().strip(),System.currentTimeMillis());
    }
    private String encode(Object value) {try{return json.writeValueAsString(value);}catch(Exception ex){throw new IllegalStateException(ex);}}
    static String hash(byte[] data) {try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data));}catch(Exception ex){throw new IllegalStateException(ex);}}
    static void bad(String message) {throw new ResponseStatusException(HttpStatus.BAD_REQUEST,message);}
}
