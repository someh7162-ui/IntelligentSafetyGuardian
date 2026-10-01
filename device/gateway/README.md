# Windows USB 网关

`riderguard_usb_gateway.ps1` 从 STM32 USB CDC 串口解析 `JPEG_TX_BEGIN LEN=...` 帧和 `GPS_NMEA` 行，分别上传图像和定位到 RiderGuard 后台。`riderguard_nmea.ps1` 校验 RMC 校验和，室内无定位时上报 `gpsValid=false` 且不伪造经纬度。

先建立到后台的 SSH 本地端口转发，并在本目录创建 `riderguard-device-token.txt`（纯文本一行；已被仓库忽略）。默认串口为 COM3，后台地址为 `http://127.0.0.1:18766`。运行 `pwsh -File riderguard_usb_gateway.ps1 -Port COM3`。如本地端口或设备 ID 不同，使用脚本参数覆盖。

令牌文件不能放入版本控制。生产环境应使用 TLS，4G 接入时由设备端实现相同协议。

图像上传失败时会重试，但拍摄超过 15 秒的待传图片会被丢弃（`-MaxImageAgeMs` 可调整），防止过期图片影响后台当前的人群风险判断。后台仍会独立检查帧时间与顺序。
