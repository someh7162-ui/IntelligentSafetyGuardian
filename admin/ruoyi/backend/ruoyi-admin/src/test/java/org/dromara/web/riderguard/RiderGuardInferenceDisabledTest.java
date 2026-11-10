package org.dromara.web.riderguard;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.PreparedStatementCreator;
import org.springframework.jdbc.core.RowMapper;
import org.springframework.jdbc.support.KeyHolder;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.web.server.ResponseStatusException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import static org.mockito.ArgumentMatchers.*;

class RiderGuardInferenceDisabledTest {
    @TempDir Path images;

    private RiderGuardService service(JdbcTemplate jdbc) {
        RiderGuardService service = new RiderGuardService(jdbc, mock(TrafficSignalService.class), mock(RiderGuardSampling.class));
        ReflectionTestUtils.setField(service, "imageDirectory", images.toString());
        ReflectionTestUtils.setField(service, "inferenceEnabled", false);
        ReflectionTestUtils.setField(service, "aiUrl", "http://127.0.0.1:1");
        return service;
    }

    @Test @SuppressWarnings("unchecked")
    void savesAndAcknowledgesTheSameSampleWithoutInferenceOrDuplicates() throws Exception {
        JdbcTemplate jdbc = mock(JdbcTemplate.class);
        String token = "a".repeat(64);
        String hash = HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(token.getBytes(java.nio.charset.StandardCharsets.US_ASCII)));
        when(jdbc.query(anyString(), any(RowMapper.class), eq("TEST-USB"))).thenReturn(List.of(hash));
        when(jdbc.queryForList(anyString(), eq("TEST-USB"), eq("cam-1")))
            .thenReturn(List.of(), List.of(Map.of("id", 71L, "inference_status", "SKIPPED")));
        when(jdbc.update(any(PreparedStatementCreator.class), any(KeyHolder.class))).thenAnswer(call -> {
            call.<KeyHolder>getArgument(1).getKeyList().add(Map.of("id", 71L));
            return 1;
        });
        RiderGuardService service = service(jdbc);
        byte[] jpeg = {(byte)0xff, (byte)0xd8, 1, 2, (byte)0xff, (byte)0xd9};
        long captured = System.currentTimeMillis();
        Map<String, Object> first = service.image("TEST-USB", "cam-1", captured, jpeg, token, null);
        Map<String, Object> second = service.image("TEST-USB", "cam-1", captured, jpeg, token, null);
        assertEquals("SAVED", first.get("status"));
        assertEquals(first, second);
        assertFalse(first.containsKey("personCount"));
        try (var paths = Files.list(images)) { assertEquals(1L, paths.count()); }
        verify(jdbc, times(1)).update(any(PreparedStatementCreator.class), any(KeyHolder.class));
        verify(jdbc).update("UPDATE rg_image SET person_count=NULL,inference_status='SKIPPED',inference_mode='disabled' WHERE id=?", 71L);
        assertTrue(mockingDetails(jdbc).getInvocations().stream().noneMatch(i ->
            i.getArguments().length > 0 && i.getArgument(0) instanceof String sql &&
            (sql.contains("INSERT INTO rg_inference_attempt") || sql.contains("INSERT IGNORE INTO rg_event"))));
    }

    @Test void missingTokenStillRejectsBeforeSaving() throws Exception {
        JdbcTemplate jdbc = mock(JdbcTemplate.class);
        assertThrows(ResponseStatusException.class, () -> service(jdbc).image("TEST-USB", "cam-1", System.currentTimeMillis(), new byte[4], null, null));
        verifyNoInteractions(jdbc);
        try (var paths = Files.list(images)) { assertEquals(0L, paths.count()); }
    }

    @Test void healthReportsDisabledWithoutContactingAi() {
        JdbcTemplate jdbc = mock(JdbcTemplate.class);
        when(jdbc.queryForObject(anyString(), eq(Long.class), anyLong())).thenReturn(0L);
        when(jdbc.queryForObject(anyString(), eq(Double.class), anyLong())).thenReturn(0.0);
        Map<String, Object> health = service(jdbc).aiHealth();
        assertEquals("DISABLED", health.get("status"));
        assertEquals("disabled", health.get("mode"));
    }
}
