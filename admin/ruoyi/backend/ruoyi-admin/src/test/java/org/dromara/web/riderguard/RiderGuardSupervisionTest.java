package org.dromara.web.riderguard;
import org.junit.jupiter.api.*;
import org.junit.jupiter.api.io.TempDir;
import org.h2.jdbcx.JdbcDataSource;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.web.server.ResponseStatusException;
import java.nio.file.Path;
import java.awt.image.BufferedImage;
import javax.imageio.ImageIO;
import java.io.ByteArrayOutputStream;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class RiderGuardSupervisionTest {
    JdbcTemplate jdbc;RiderGuardSupervisionService service;RiderGuardSupervisionDeviceController controller;
    @TempDir Path temp;
    final String token="1".repeat(64);
    @BeforeEach void init(){
        var ds=new JdbcDataSource();ds.setURL("jdbc:h2:mem:"+UUID.randomUUID()+";MODE=MySQL;DATABASE_TO_LOWER=TRUE;DB_CLOSE_DELAY=-1");
        jdbc=new JdbcTemplate(ds);
        jdbc.execute("CREATE TABLE rg_device(device_id VARCHAR(64) PRIMARY KEY, token_hash VARCHAR(64),rider_id BIGINT)");
        jdbc.execute("CREATE TABLE rg_rider(id BIGINT PRIMARY KEY,name VARCHAR(100))");
        jdbc.update("INSERT INTO rg_device VALUES (?,?,NULL)","TEST",RiderGuardSupervisionService.hash(token.getBytes(java.nio.charset.StandardCharsets.UTF_8)));
        service=new RiderGuardSupervisionService(jdbc);ReflectionTestUtils.setField(service,"imageDirectory",temp.toString());service.initialize();
        controller=new RiderGuardSupervisionDeviceController(new RiderGuardService(jdbc,mock(TrafficSignalService.class),mock(RiderGuardSampling.class)),service);
    }
    RiderGuardSupervisionService.Event event(String id,String kind,boolean mock){return new RiderGuardSupervisionService.Event(id,kind,1,1794100000000L,1000,4000,12000,true,34.0,108.0,0L,20.0,12.0,21.0,15.0,-4.0,1L,false,mock,List.of(new RiderGuardSupervisionService.Trace(0,-4,0,0)));}
    RiderGuardSupervisionService.Batch batch(RiderGuardSupervisionService.Event...events){return new RiderGuardSupervisionService.Batch("TEST","group1",List.of(events));}
    byte[] jpeg() throws Exception {var out=new ByteArrayOutputStream();ImageIO.write(new BufferedImage(16,16,BufferedImage.TYPE_INT_RGB),"jpg",out);return out.toByteArray();}
    @Test void noImplicitSpeedLimitAndPolicyHistory(){assertNull(service.policy().speedLimitKph());var p=service.policy();var saved=service.savePolicy(new RiderGuardSupervisionService.Policy(p.version(),true,15.0,3000,5000,600000,3,3,200,1,2500,3000,3,1100));assertEquals(2,saved.version());assertEquals(2,jdbc.queryForObject("SELECT COUNT(*) FROM rg_supervision_policy",Integer.class));}
    @Test void badTokensRejectedBeforeAnyInsertion(){for(String t:new String[]{"","2".repeat(64)})assertThrows(ResponseStatusException.class,()->controller.events(batch(event("e1","HARSH_BRAKE",false)),t));assertThrows(ResponseStatusException.class,()->controller.policy("TEST",null));assertEquals(0,jdbc.queryForObject("SELECT COUNT(*) FROM rg_safety_event",Integer.class));}
    @Test void sameEventRetryIsIdempotentAndMutationRejected(){var e=event("e1","HARSH_BRAKE",false);controller.events(batch(e),token);controller.events(batch(e),token);assertEquals(1,jdbc.queryForObject("SELECT COUNT(*) FROM rg_safety_event",Integer.class));assertThrows(ResponseStatusException.class,()->controller.events(batch(event("e1","INTERSECTION_REVIEW",false)),token));}
    @Test void mockRowsSeparatedAndNoAutomaticVerdict(){controller.events(batch(event("real","HARSH_BRAKE",false),event("fake","HARSH_BRAKE",true)),token);assertEquals(1,service.events(false).size());assertEquals(1,service.events(true).size());assertEquals("PENDING",service.events(false).getFirst().get("status"));}
    @Test void unconfiguredOverspeedCannotBecomeEvidence(){assertThrows(ResponseStatusException.class,()->controller.events(batch(event("e1","OVERSPEED",false)),token));}
    @Test void jpegRetryAndReviewAudit() throws Exception {
        controller.events(batch(event("red","INTERSECTION_REVIEW",false)),token);long id=((Number)service.events(false).getFirst().get("id")).longValue();byte[] bytes=jpeg();
        controller.photo("TEST","red","p1",1794100000000L,token,bytes);controller.photo("TEST","red","p1",1794100000000L,token,bytes);
        assertEquals(1,jdbc.queryForObject("SELECT COUNT(*) FROM rg_safety_image",Integer.class));
        assertThrows(ResponseStatusException.class,()->service.review(id,new RiderGuardSupervisionService.Review("CONFIRMED","只有一张图"),1,"reviewer"));
        controller.photo("TEST","red","p2",1794100001100L,token,bytes);service.review(id,new RiderGuardSupervisionService.Review("CONFIRMED","逐帧核对了适用灯组与通行过程"),1,"reviewer");
        assertEquals("CONFIRMED",service.detail(id).get("status"));assertEquals(1,((List<?>)service.detail(id).get("reviews")).size());
        long imageId=((Number)((Map<?,?>)((List<?>)service.detail(id).get("photos")).getFirst()).get("id")).longValue();assertArrayEquals(bytes,service.photo(imageId));
        assertThrows(ResponseStatusException.class,()->controller.photo("TEST","red","p3",null,"2".repeat(64),bytes));
    }
    @Test void invalidPayloadsFailBeforeSaving(){assertThrows(ResponseStatusException.class,()->service.saveRoads(List.of(new RiderGuardSupervisionService.Road(1L,"x",Double.NaN,1,60,"直行","",true))));assertThrows(ResponseStatusException.class,()->service.ingest(batch(event("../x","HARSH_BRAKE",false))));}
}
