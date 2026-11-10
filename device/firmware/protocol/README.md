# Official MCU protocol helpers

`rg_gps.c/.h` and `rg_telemetry.c/.h` are portable C modules for the official RiderGuard telemetry contract. They do not depend on HAL, allocate a network connection, contain a device token or start a cellular upload.

- Feed checksum-validated RMC sentences into `rg_gps_parse_rmc` with the same monotonic tick used for freshness checks.
- Supply a registered device ID, unique sample ID and a trusted Unix millisecond capture time to `rg_telemetry_build_json`.
- Keep the same sample ID/time for retries; use a new boot identifier after restart.
- A stale/invalid fix emits null coordinates and `gpsValid=false`; do not treat LBS as satellite positioning.
- Send the resulting JSON to `/device/riderguard/telemetry` with `X-Device-Token` over HTTPS, except during loopback tests.

These helpers are intentionally not auto-linked into the current camera/GPS build. Local hardware testing uses the unchanged USB firmware and `device/gateway/local_gateway.py`. A future cellular transport must also implement the official multipart image upload and policy response handling.
