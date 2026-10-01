param(
    [string]$Port = 'COM3',
    [string]$DeviceId = 'STM32-CAM-001',
    [string]$TokenFile = (Join-Path $PSScriptRoot 'riderguard-device-token.txt'),
    [string]$Endpoint = 'http://127.0.0.1:18766/prod-api/device/riderguard/image',
    [string]$GpsEndpoint = 'http://127.0.0.1:18766/prod-api/device/riderguard/telemetry',
    [int]$ImageIntervalMs = 4000,
    [int]$GpsIntervalMs = 10000
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'riderguard_nmea.ps1')
if (-not (Test-Path -LiteralPath $TokenFile)) {
    throw "Device token file not found: $TokenFile"
}
$deviceToken = (Get-Content -LiteralPath $TokenFile -Raw).Trim()
if (-not $deviceToken) { throw 'Device token file is empty' }

$marker = [Text.Encoding]::ASCII.GetBytes('JPEG_TX_BEGIN LEN=')
$gpsMarker = [Text.Encoding]::ASCII.GetBytes('GPS_NMEA ')
$buffer = [System.Collections.Generic.List[byte]]::new()
$chunk = [byte[]]::new(8192)
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.ReadTimeout = 1000
$serial.ReadBufferSize = 65536
$client = [System.Net.Http.HttpClient]::new()
$client.Timeout = [TimeSpan]::FromSeconds(15)
$nextCaptureMs = 0L
$retryAtMs = 0L
$pending = $null
$frameNumber = 0L
$imageRetryDelayMs = 3000
$gpsPending = $null
$gpsRetryAtMs = 0L
$nextGpsMs = 0L
$gpsNumber = 0L
$gpsRetryDelayMs = 3000

function Find-Marker([System.Collections.Generic.List[byte]]$Bytes, [byte[]]$Needle) {
    for ($position = 0; $position -le $Bytes.Count - $Needle.Length; $position++) {
        $matches = $true
        for ($offset = 0; $offset -lt $Needle.Length; $offset++) {
            if ($Bytes[$position + $offset] -ne $Needle[$offset]) { $matches = $false; break }
        }
        if ($matches) { return $position }
    }
    return -1
}

function Queue-Rmc([string]$Line) {
    $fix = ConvertFrom-RmcSentence $Line
    if ($null -eq $fix) { return }
    $now = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    if ($null -ne $script:gpsPending -or $now -lt $script:nextGpsMs) { return }
    $script:gpsNumber++
    $script:gpsPending = [pscustomobject]@{
        SampleId = "gps-$now-$($script:gpsNumber)"
        CapturedAtMs = $now
        Fix = $fix
    }
    $script:gpsRetryAtMs = 0L
    $script:gpsRetryDelayMs = 3000
}

function Send-Telemetry($item) {
    $payload = @{
        deviceId = $DeviceId; sampleId = $item.SampleId
        capturedAtMs = $item.CapturedAtMs
        latitude = $item.Fix.latitude; longitude = $item.Fix.longitude
        speedKph = $item.Fix.speedKph; heading = $item.Fix.heading
        gpsAccuracy = $item.Fix.gpsAccuracy; gpsValid = $item.Fix.gpsValid
    } | ConvertTo-Json -Compress
    $request = [System.Net.Http.HttpRequestMessage]::new('POST', $GpsEndpoint)
    $response = $null
    try {
        $request.Headers.Add('X-Device-Token', $deviceToken)
        $request.Content = [System.Net.Http.StringContent]::new($payload, [Text.Encoding]::UTF8, 'application/json')
        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        $body = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) { throw "HTTP $([int]$response.StatusCode): $body" }
        if ($item.Fix.gpsValid) {
            Write-Host ('GPS valid: {0:F6}, {1:F6}; {2:F1} km/h' -f `
                $item.Fix.latitude, $item.Fix.longitude, $item.Fix.speedKph)
        } else {
            Write-Host 'GPS has no fix (indoor/weak signal); no coordinates sent.'
        }
        return $true
    } catch {
        Write-Warning "GPS upload failed: $($_.Exception.Message)"
        return $false
    } finally {
        if ($null -ne $response) { $response.Dispose() }
        $request.Dispose()
    }
}

function Send-Image($item) {
    $form = [System.Net.Http.MultipartFormDataContent]::new()
    $response = $null
    try {
        $form.Add([System.Net.Http.StringContent]::new($DeviceId), 'deviceId')
        $form.Add([System.Net.Http.StringContent]::new($item.SampleId), 'sampleId')
        $form.Add([System.Net.Http.StringContent]::new([string]$item.CapturedAtMs), 'capturedAtMs')
        $image = [System.Net.Http.ByteArrayContent]::new($item.Jpeg)
        $image.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::new('image/jpeg')
        $form.Add($image, 'image', 'camera.jpg')
        $request = [System.Net.Http.HttpRequestMessage]::new('POST', $Endpoint)
        try {
            $request.Headers.Add('X-Device-Token', $deviceToken)
            $request.Content = $form
            $response = $client.SendAsync($request).GetAwaiter().GetResult()
            $body = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            if (-not $response.IsSuccessStatusCode) {
                throw "HTTP $([int]$response.StatusCode): $body"
            }
            $result = $body | ConvertFrom-Json
            if ($result.status -eq 'ERROR') { throw "Inference error: $body" }
            Write-Host ("Frame {0}: status={1}, people={2}, imageId={3}" -f `
                $item.FrameNumber, $result.status, $result.personCount, $result.imageId)
            return $true
        } finally {
            $request.Dispose()
        }
    } catch {
        Write-Warning "Frame $($item.FrameNumber) upload failed: $($_.Exception.Message)"
        return $false
    } finally {
        if ($null -ne $response) { $response.Dispose() }
        $form.Dispose()
    }
}

try {
    $serial.Open()
    Write-Host "Reading $Port; forwarding JPEG and GNSS to RiderGuard. Press Ctrl+C to stop."
    while ($true) {
        $nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        if ($null -ne $pending -and $nowMs - $pending.CapturedAtMs -gt 300000) {
            Write-Warning "Discarding stale frame $($pending.FrameNumber) after five minutes offline."
            $pending = $null
        }
        if ($null -ne $gpsPending -and $nowMs - $gpsPending.CapturedAtMs -gt 300000) {
            Write-Warning 'Discarding stale GPS sample after five minutes offline.'
            $gpsPending = $null
        }
        if ($null -ne $pending -and $nowMs -ge $retryAtMs) {
            if (Send-Image $pending) {
                $pending = $null
                $imageRetryDelayMs = 3000
                $nextCaptureMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() + $ImageIntervalMs
            } else {
                $retryAtMs = $nowMs + $imageRetryDelayMs
                $imageRetryDelayMs = [math]::Min($imageRetryDelayMs * 2, 30000)
            }
        }
        if ($null -ne $gpsPending -and $nowMs -ge $gpsRetryAtMs) {
            if (Send-Telemetry $gpsPending) {
                $gpsPending = $null
                $gpsRetryDelayMs = 3000
                $nextGpsMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() + $GpsIntervalMs
            } else {
                $gpsRetryAtMs = $nowMs + $gpsRetryDelayMs
                $gpsRetryDelayMs = [math]::Min($gpsRetryDelayMs * 2, 30000)
            }
        }

        try { $count = $serial.Read($chunk, 0, $chunk.Length) }
        catch [System.TimeoutException] { continue }
        for ($i = 0; $i -lt $count; $i++) { $buffer.Add($chunk[$i]) }

        while ($true) {
            $imageStart = Find-Marker $buffer $marker
            $gpsStart = Find-Marker $buffer $gpsMarker
            $isGps = $gpsStart -ge 0 -and ($imageStart -lt 0 -or $gpsStart -lt $imageStart)
            $start = if ($isGps) { $gpsStart } else { $imageStart }
            if ($start -lt 0) {
                $keep = [math]::Max($marker.Length, $gpsMarker.Length) - 1
                if ($buffer.Count -gt $keep) { $buffer.RemoveRange(0, $buffer.Count - $keep) }
                break
            }
            if ($start -gt 0) { $buffer.RemoveRange(0, $start) }
            $newline = -1
            $prefixLength = if ($isGps) { $gpsMarker.Length } else { $marker.Length }
            for ($i = $prefixLength; $i -lt $buffer.Count; $i++) {
                if ($buffer[$i] -eq 10) { $newline = $i; break }
            }
            if ($newline -lt 0) {
                if ($buffer.Count -gt 160) { $buffer.Clear() }
                break
            }
            $header = [Text.Encoding]::ASCII.GetString($buffer.GetRange(0, $newline + 1).ToArray()).Trim()
            if ($isGps) {
                $buffer.RemoveRange(0, $newline + 1)
                Queue-Rmc $header.Substring($gpsMarker.Length)
                continue
            }
            $match = [regex]::Match($header, '^JPEG_TX_BEGIN LEN=([0-9]+)$')
            if (-not $match.Success) { $buffer.RemoveRange(0, $newline + 1); continue }
            $length = [int]$match.Groups[1].Value
            if ($length -lt 4 -or $length -gt 1048576) {
                $buffer.RemoveRange(0, $newline + 1)
                continue
            }
            $dataStart = $newline + 1
            if ($buffer.Count -lt $dataStart + $length) { break }
            $jpeg = [byte[]]::new($length)
            $buffer.CopyTo($dataStart, $jpeg, 0, $length)
            $buffer.RemoveRange(0, $dataStart + $length)
            $frameNumber++
            if ($null -ne $pending -or [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() -lt $nextCaptureMs) { continue }
            if ($jpeg[0] -ne 0xff -or $jpeg[1] -ne 0xd8 -or $jpeg[$length - 2] -ne 0xff -or $jpeg[$length - 1] -ne 0xd9) {
                Write-Warning "Frame $frameNumber has invalid JPEG markers"
                continue
            }
            $capturedAtMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
            $pending = [pscustomobject]@{
                FrameNumber = $frameNumber
                SampleId = "cam-$capturedAtMs-$frameNumber"
                CapturedAtMs = $capturedAtMs
                Jpeg = $jpeg
            }
            $retryAtMs = 0L
            $imageRetryDelayMs = 3000
        }
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    $client.Dispose()
}
