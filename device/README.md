# 当前本机接入

正式网页控制入口见 [本机硬件联调](../docs/RiderGuard_本机硬件联调.md)。USB 网关位于 `gateway/local_gateway.py`，可复用 MCU 协议位于 `firmware/protocol/`。本机测试不启动 4G 上报。

# Device

设备端按传感器、视觉、风险判断和传输拆分。开发阶段可先实现 `simulator.py`，以固定频率生成 GPS/IMU 数据，验证云边链路后再接入 GPIO、串口和摄像头。

