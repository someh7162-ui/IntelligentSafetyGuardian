# 正式设备 USB 网关

本机网页入口及步骤见 [本机硬件联调](../../docs/RiderGuard_本机硬件联调.md)。

- `local_gateway.py` 是唯一的 USB 拆包与正式 HTTP 上传实现；由后台受控启动，凭证通过子进程环境传递。
- `requirements.txt` 锁定串口依赖；项目启动器会准备隔离环境。
- `riderguard_usb_gateway.ps1` 保留为手动命令行兼容入口，默认定位 3 秒、图片 30 秒，直接连接正式后端接口。
- `riderguard_nmea.ps1` 保留供既有脚本兼容，网页受控链路使用 Python 解析器。
- `test_local_gateway.py` 验证 NMEA、二进制 JPEG 拆包、正式上传和凭证重定向保护。

USB 接入不会向 4G 模块发送指令。自动传图在每次网页启动连接时默认关闭。退出后台或点击停止会释放串口；离线重试当前使用有界内存队列。
