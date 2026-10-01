# STM32 摄像头、GPS 与管理后台联动

当前链路：STM32F407 的 OV2640 JPEG 和 ATGM336H NMEA，经 USB CDC 到 Windows 网关；网关通过本机 SSH 端口转发到若依后端；后端调用服务器 wt 目录下的 YOLO 服务；单帧检测到至少 3 人时记录 `CROWD_DENSITY` 风险事件。GPS 室内无定位时上传 `gpsValid=false`，后台不展示虚构坐标或速度。

## 源码位置

- `device/firmware/stm32f407/`：STM32CubeIDE 工程，默认 `GNSS_TEST_MODE=0`，摄像头 JPEG 与 GPS RMC 同时通过 USB CDC 输出。GNSS 使用 USART2 9600 8N1，PA3 接模块 TX，PA2 接模块 RX。MPU6050 使用 I²C1 PB8/PB9，OV2640 使用 I²C2、DCMI 和 DMA。
- `device/gateway/`：Windows PowerShell USB 网关和 NMEA 解析器。
- `admin/ruoyi/`：设备图像/定位接入、YOLO 人数结果、风险事件和管理页面。
- `deploy/wt/`：当前服务器部署脚本与 Nginx/AI 适配器源码。运行时程序、数据库、模型、上传文件及密钥文件不入库。

## 后台实时画面

管理后台侧栏的“实时画面”打开 `/riderguard/camera`。选择骑手后，页面每 2 秒检查该设备的最新图片编号；仅在有新图片时通过已登录的图片接口读取 JPEG。页面标明拍摄时间、离线与过期状态，切换标签页或关闭页面后停止拉取。当前是持续更新的照片，不是视频流，也不会向设备发出即时拍照命令；电脑网关必须持续连接并上传。
## 本地网关

1. 在电脑上连接开发板，确认 USB CDC 端口号。将设备令牌单独写到 `device/gateway/riderguard-device-token.txt`；这个文件已忽略，切勿提交。令牌须与后台设备配置一致。
2. 用 SSH 本地转发访问服务器，只绑定电脑回环地址，例如：

   `ssh -N -L 127.0.0.1:18766:127.0.0.1:18766 -p 6000 <user>@<server>`

3. 浏览器访问 `http://127.0.0.1:18766/`，然后运行：

   `pwsh -File device/gateway/riderguard_usb_gateway.ps1 -Port COM3`

   可通过 `-Endpoint`、`-GpsEndpoint` 和 `-TokenFile` 改为其他环境。保持 SSH 转发窗口和网关进程运行。电脑的 HTTP 代理端口 7897 只用于本机下载依赖；USB 上传链路不依赖它。

## 后续切换 4G

保持后台现有 `/device/riderguard/image` 和 `/device/riderguard/telemetry` 报文与设备令牌鉴权。4G 模块加入后，由板端网络栈直接实现这些 HTTP 请求，并替代 Windows USB 网关；YOLO 和管理页面无需改变接口。正式外网访问前需要给入口配置 HTTPS、访问控制和密钥轮换。

## 验证边界

STM32 采集、USB JPEG 和室内 GPS 无定位报文已在板上验证；服务器上曾用三人测试图验证风险事件。当前实际摄像头画面较暗，真人现场识别率仍需校准。4G 模块尚未接入，板端直接上传尚未实现。
