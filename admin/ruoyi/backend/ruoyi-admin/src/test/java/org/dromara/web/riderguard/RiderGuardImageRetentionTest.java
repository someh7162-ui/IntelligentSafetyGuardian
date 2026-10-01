package org.dromara.web.riderguard;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.util.ReflectionTestUtils;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.startsWith;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

class RiderGuardImageRetentionTest {
    @TempDir Path images;

    @Test
    void removesExpiredOrdinaryImageAndItsInferenceAttempts() throws Exception {
        JdbcTemplate jdbc = mock(JdbcTemplate.class);
        String fileName = "47ed094c-81c0-4575-b482-7876e1bd5729.jpg";
        Files.write(images.resolve(fileName), new byte[] {1, 2, 3});
        when(jdbc.queryForList(anyString(), anyLong(), anyInt()))
            .thenReturn(List.of(Map.of("id", 7L, "file_name", fileName)));
        when(jdbc.update(startsWith("DELETE FROM rg_image"), eq(7L), eq(7L))).thenReturn(1);
        RiderGuardImageRetention retention = new RiderGuardImageRetention(jdbc);
        ReflectionTestUtils.setField(retention, "imageDirectory", images.toString());
        ReflectionTestUtils.setField(retention, "retentionDays", 3);
        ReflectionTestUtils.setField(retention, "batchSize", 500);

        assertEquals(1, retention.cleanupOnce());
        assertFalse(Files.exists(images.resolve(fileName)));
        verify(jdbc).update("DELETE FROM rg_inference_attempt WHERE image_id=?", 7L);
    }

    @Test
    void keepsFileWhenEvidenceAppearsBeforeDeletion() throws Exception {
        JdbcTemplate jdbc = mock(JdbcTemplate.class);
        String fileName = "47ed094c-81c0-4575-b482-7876e1bd5729.jpg";
        Files.write(images.resolve(fileName), new byte[] {1});
        when(jdbc.queryForList(anyString(), anyLong(), anyInt()))
            .thenReturn(List.of(Map.of("id", 7L, "file_name", fileName)));
        RiderGuardImageRetention retention = new RiderGuardImageRetention(jdbc);
        ReflectionTestUtils.setField(retention, "imageDirectory", images.toString());
        ReflectionTestUtils.setField(retention, "retentionDays", 3);
        ReflectionTestUtils.setField(retention, "batchSize", 500);

        assertEquals(0, retention.cleanupOnce());
        org.junit.jupiter.api.Assertions.assertTrue(Files.exists(images.resolve(fileName)));
    }
}
