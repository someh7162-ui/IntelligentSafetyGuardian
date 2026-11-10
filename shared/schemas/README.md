# 正式共享协议

当前硬件与正式若依后台统一采用：

- `riderguard-telemetry-v1.schema.json`：定位、速度、采样编号、采集时间与 GPS 有效性。
- 图片接口使用 multipart，字段固定为 `deviceId`、`sampleId`、`capturedAtMs`、`image`。
- 两个设备接口都要求 `X-Device-Token`，公网 HTTPS、本机回环允许 HTTP。

接口及本机网页测试步骤见 [本机硬件联调](../../docs/RiderGuard_本机硬件联调.md)。旧原型 `location_event` 等示例不作为正式设备接口契约。LBS、IMU 与电量后续按版本扩展，不混入 GPS 有效坐标。
