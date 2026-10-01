function ConvertFrom-NmeaCoordinate {
    param([string]$Value, [string]$Hemisphere, [bool]$IsLatitude)

    $number = 0.0
    if (-not [double]::TryParse($Value, [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) { return $null }
    $degrees = [math]::Floor($number / 100)
    $minutes = $number - 100 * $degrees
    if ($minutes -lt 0 -or $minutes -ge 60) { return $null }
    if ($IsLatitude) {
        if ($Hemisphere -notin @('N', 'S') -or $degrees -gt 90) { return $null }
    } else {
        if ($Hemisphere -notin @('E', 'W') -or $degrees -gt 180) { return $null }
    }
    $decimal = $degrees + $minutes / 60
    if ($Hemisphere -in @('S', 'W')) { $decimal = -$decimal }
    return $decimal
}

function ConvertFrom-RmcSentence {
    param([string]$Sentence)

    $match = [regex]::Match($Sentence.Trim(), '^\$([A-Z]{2}RMC,[^*]+)\*([0-9A-Fa-f]{2})$')
    if (-not $match.Success) { return $null }
    $checksum = 0
    foreach ($character in $match.Groups[1].Value.ToCharArray()) {
        $checksum = $checksum -bxor [int][char]$character
    }
    if ($checksum -ne [Convert]::ToInt32($match.Groups[2].Value, 16)) { return $null }
    $fields = $match.Groups[1].Value.Split(',')
    if ($fields.Length -lt 10) { return $null }
    if ($fields[2] -eq 'V') {
        return [pscustomobject]@{
            gpsValid = $false; latitude = $null; longitude = $null
            speedKph = 0.0; heading = $null; gpsAccuracy = $null
        }
    }
    if ($fields[2] -ne 'A') { return $null }
    $latitude = ConvertFrom-NmeaCoordinate $fields[3] $fields[4] $true
    $longitude = ConvertFrom-NmeaCoordinate $fields[5] $fields[6] $false
    if ($null -eq $latitude -or $null -eq $longitude -or -not $fields[7]) { return $null }
    $knots = 0.0
    if (-not [double]::TryParse($fields[7], [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$knots)) { return $null }
    $speedKph = $knots * 1.852
    if ($speedKph -lt 0 -or $speedKph -gt 150) { return $null }
    $heading = $null
    if ($fields[8]) {
        $course = 0.0
        if ([double]::TryParse($fields[8], [Globalization.NumberStyles]::Float,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$course) -and
            $course -ge 0 -and $course -lt 360) { $heading = $course }
    }
    return [pscustomobject]@{
        gpsValid = $true; latitude = $latitude; longitude = $longitude
        speedKph = $speedKph; heading = $heading; gpsAccuracy = $null
    }
}
