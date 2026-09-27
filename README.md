# 外卖骑手智能安全守护系统

这是一个“车载安全守护盒 + 云端服务 + 管理后台 + AI 分析”的单仓库项目。

## 项目分层

- `device`：STM32 V1 的设备协议、限速提醒 C 模块和 Python 联调模拟器；视觉识别运行在云端。
- `admin`：基于 RuoYi-Vue-Plus 的正式管理平台（Spring Boot + Vue 3），负责权限、设备、骑手、轨迹和风险事件。
- `backend`：当前 FastAPI 原型和后续 AI/设备接入适配服务。
- `ai`：视觉检测、道路分割、多传感器融合和云端 VLM 分析。
- `frontend`：当前 Vue 最小原型，正式后台以前端框架中的管理端为准。
- `shared`：前后端、设备端共用的数据协议和示例报文。
- `deploy`：本地开发和部署配置（MQTT、数据库、反向代理）。
- `docs`：项目级硬件、软件、AI、框架和界面设计文档。
- `logs`：开发变更记录。

## 建议实现顺序

1. 先用 STM32 V1 模拟数据打通 `device -> MQTT/HTTP -> backend -> frontend`。
2. 增加轨迹和风险事件数据库，以及 WebSocket 实时刷新。
3. 接入真实 GPS、IMU、摄像头和低频抓拍上传。
4. 加入路线偏离、多传感器风险评分和云端 VLM 解释。

## 当前状态

当前若依后台已接入骑手、设备、定位轨迹、分场景限速、风险事件和私有照片访问；Python 识别服务支持模拟模式及按权重切换的 YOLO 模式。真实 STM32 外设驱动、GPU 模型权重和高德地图 Key 仍需按实物与账号配置。联调步骤见 [`docs/RiderGuard_设备云端联调.md`](docs/RiderGuard_设备云端联调.md)。

## Windows 本地数据库

本机开发环境使用 MySQL 8.4.11 和社区维护的 Redis 8.4.7 Windows 版。程序、下载包和数据放在 `deploy/local-runtime/`，该目录已加入 `.gitignore`。MySQL 只监听本机 3306，Redis 只监听本机 6379；配置与后端 `application-dev.yml` 一致。

首次初始化已导入 `ry_vue.sql`、`ry_workflow.sql`、`ry_job.sql` 和 `ry_ai.sql`。以后运行 `admin/ruoyi/start_riderguard.bat`，或单独运行 `scripts/start_local_services.ps1`，即可拉起本地数据库。若依后端启动后，可以访问 `http://127.0.0.1/dev-api/auth/code` 检查验证码接口。

MySQL 包来自 [Oracle MySQL 官方下载](https://dev.mysql.com/downloads/mysql/)，Redis 包来自 [redis-windows 社区发行页](https://github.com/redis-windows/redis-windows/releases/tag/8.4.7)。Redis 这个 Windows 移植版用于本地开发。

项目级设计文档请查看 [`docs/`](docs/README.md)。


## 从 GitHub 获取后配置

仓库包含管理前后端源码、AI 服务、设备模拟器及项目文档。依赖缓存、本机数据库、模型权重、日志和本地凭证不随仓库上传。

首次运行时，把 `admin/ruoyi/frontend/.env.development.example`、`.env.production.example` 分别复制为同目录下的 `.env.development`、`.env.production`，根据实际后端填写匹配的请求加解密配置。高德 Web Key 放入 `.env.local`，安全密钥按设备云端联调文档配置到后端。本地数据库和设备凭证需要自行初始化。

`admin/ruoyi/frontend` 与 `admin/ruoyi/backend` 的源码已作为普通目录纳入本仓库，无需初始化子模块。原框架许可证保留在对应目录。
