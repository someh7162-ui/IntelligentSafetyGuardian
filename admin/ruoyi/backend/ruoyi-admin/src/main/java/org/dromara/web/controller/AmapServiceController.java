package org.dromara.web.controller;

import jakarta.servlet.http.HttpServletRequest;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestMethod;
import org.springframework.web.bind.annotation.RestController;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.regex.Pattern;

/** Keeps the AMap JS API security code on the server while forwarding its service calls. */
@RestController
public class AmapServiceController {

    private static final String PREFIX = "/_AMapService";
    private static final Pattern ALLOWED_PATH = Pattern.compile("^/(?:v3/[A-Za-z0-9_./-]+|v4/map/styles[A-Za-z0-9_./-]*)$");
    private static final HttpClient CLIENT = HttpClient.newBuilder()
        .connectTimeout(Duration.ofSeconds(5))
        .build();

    @Value("${RIDERGUARD_AMAP_SECURITY_FILE:}")
    private String securityFile;

    @RequestMapping(value = "/_AMapService/**", method = {RequestMethod.GET, RequestMethod.POST})
    public ResponseEntity<byte[]> proxy(HttpServletRequest request) {
        String path = request.getRequestURI().substring(PREFIX.length());
        String query = request.getQueryString();
        if (!ALLOWED_PATH.matcher(path).matches() || path.contains("..") || path.contains("//")
            || (query != null && Pattern.compile("(?:^|&)jscode=", Pattern.CASE_INSENSITIVE).matcher(query).find())) {
            return ResponseEntity.badRequest().build();
        }

        String securityCode;
        try {
            securityCode = securityFile.isBlank() ? "" : Files.readString(Path.of(securityFile), StandardCharsets.UTF_8).trim();
        } catch (IOException | RuntimeException ex) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        }
        if (!securityCode.matches("[0-9a-fA-F]{32}")) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        }

        String origin = path.startsWith("/v4/map/styles") ? "https://webapi.amap.com" : "https://restapi.amap.com";
        URI upstream;
        try {
            upstream = URI.create(origin + path + "?" + (query == null || query.isEmpty() ? "" : query + "&") + "jscode=" + securityCode);
        } catch (IllegalArgumentException ex) {
            return ResponseEntity.badRequest().build();
        }

        try {
            HttpRequest.Builder builder = HttpRequest.newBuilder(upstream).timeout(Duration.ofSeconds(15));
            String contentType = request.getContentType();
            if (contentType != null) builder.header(HttpHeaders.CONTENT_TYPE, contentType);
            HttpRequest outbound = "POST".equals(request.getMethod())
                ? builder.POST(HttpRequest.BodyPublishers.ofByteArray(request.getInputStream().readAllBytes())).build()
                : builder.GET().build();
            HttpResponse<byte[]> response = CLIENT.send(outbound, HttpResponse.BodyHandlers.ofByteArray());
            ResponseEntity.BodyBuilder result = ResponseEntity.status(response.statusCode());
            response.headers().firstValue(HttpHeaders.CONTENT_TYPE).ifPresent(value -> result.header(HttpHeaders.CONTENT_TYPE, value));
            return result.body(response.body());
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            return ResponseEntity.status(HttpStatus.BAD_GATEWAY).build();
        } catch (IOException | IllegalArgumentException ex) {
            return ResponseEntity.status(HttpStatus.BAD_GATEWAY).build();
        }
    }
}
