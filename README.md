# 外卖骑手智能安全守护系统

## 2026年11月11日摄像头更新

企业监管本机联调固件更新至[2.0.2-local-vga](device/firmware/stm32f407/releases/2.0.2-local-vga/)，OV2640的JPEG输出由320×240提升至640×480。实机连续3张照片解码成功，每张约16KB，采集期间IMU约100.02Hz；4G业务上传和外置Flash写入仍关闭。新增本机单次拍照诊断入口，不生成监管事件、不自动循环拍摄。

源码及固件见[固件说明](device/firmware/stm32f407/README_ENTERPRISE.md)，测试边界见[分辨率验证记录](docs/validation/20261111_OV2640分辨率验证.md)。实际照片和芯片备份留在本机，未上传仓库；论文与学校模板本次未改动。

## 2026年11月10日更新

当前工作重点为企业监管原型：急刹抓拍、超速累计和试验路口拍照留证，由企业人工复核。急刹不直接认定违规，照片里出现红灯也不直接认定闯红灯。

| 内容 | 入口 |
|---|---|
| 后续执行方案与验收边界 | [企业监管执行方案](docs/plans/企业监管后续执行方案_20261110.md) |
| 毕业论文与原始学校模板 | [论文目录](docs/thesis/README.md) |
| 段前段后、公式、目录和引用要求 | [格式要求与现稿处理](docs/thesis/格式要求与现稿处理.md) |
| 企业监管固件源码与构建 | [固件说明](device/firmware/stm32f407/README_ENTERPRISE.md) |
| 实际烧录的无凭证联调版本 | [2.0.1-local](device/firmware/stm32f407/releases/2.0.1-local/) |
| 正式管理系统 | [若依后端](admin/ruoyi/backend/)、[管理网页](admin/ruoyi/frontend/) |
| 模块实物图与原理图参考 | [硬件资料](docs/hardware/) |
| 本次归档与构建检查 | [验证记录](docs/validation/20261110_归档验证.md) |

企业固件已实机烧录并完成回读验证：IMU约99.85Hz、静止标定完成、摄像头初始化成功。该构建尚未启用4G业务上传或持久存储；GNSS当次未有效定位，语音和道路试验待验收。软件状态机测试通过与整机联调完成是不同的验收项。

此次归档包含最新固件、企业监管管理端源码、USB网关、论文和学校原始表格。论文是此前已排版版本，未将本次新硬件结果自动写入；需要按执行方案继续更新试验章节。实际部署配置、SSH及数据库密码、设备Token、地图安全配置、运行数据库、历史设备照片、芯片备份和依赖缓存不随仓库上传。现有后端配置以仓库公开样例为基础，联调时须在私有环境补齐配置。

## 历史项目说明

以下内容保留早期架构与联调背景；实施状态、传图周期及验收边界以本页上方的2026年11月10日执行方案为准。不要直接运行历史一键部署脚本。

这是一个“车载安全守护盒 + 云端服务 + 管理后台 + AI 分析”的单仓库项目。

**现行实施基线：[RiderGuard 当前实施方案](docs/RiderGuard_当前实施方案.md)。** STM32F407 负责采集、4G 上报和有效策略下的本地蜂鸣器/状态灯提醒；YOLO 行人检测运行在云端 Python 服务；若依负责骑手、设备、轨迹、风险事件和处置。旧的板端 YOLO、树莓派主控及固定 2～3 秒传图方案已取消。

## 本机硬件联调入口（2026-11-03）

双击根目录 `start_riderguard.bat`，在正式后台的“规则与运维 → 本机设备接入”选择设备和 COM 端口即可开始/停止 USB 接入、手动传图或开启自动传图。定位默认约 3 秒，自动图片默认约 30 秒。启动不自动连接串口；本阶段不启用板端 4G、不部署服务器。详见 [本机硬件联调](docs/RiderGuard_本机硬件联调.md)。

## 项目分层

- `device`：STM32 V1 的设备协议、限速提醒 C 模块和 Python 联调模拟器；视觉识别运行在云端。
- `admin`：基于 RuoYi-Vue-Plus 的正式管理平台（Spring Boot + Vue 3），负责权限、设备、骑手、轨迹和风险事件。
- `backend`：当前 FastAPI 原型和后续 AI/设备接入适配服务。
- `ai`：首版云端 YOLO 人数检测；道路分割和 VLM 分析为后续阶段。
- `frontend`：当前 Vue 最小原型，正式后台以前端框架中的管理端为准。
- `shared`：前后端、设备端共用的数据协议和示例报文。
- `deploy`：本地开发和部署配置（MQTT、数据库、反向代理）。
- `docs`：项目级硬件、软件、AI、框架和界面设计文档。
- `logs`：开发变更记录。

## 建议实现顺序

1. 用模拟设备打通 `设备 → 若依 HTTP 接口 → Python 模拟识别 → 限速返回/事件 → 管理后台`。
2. 接入真实 GNSS 定位与测速、OV2640 和本地提醒；先以 USB 网关验证正式接口，定位约 3 秒、自动 JPEG 约 30 秒；4G 直连待公网入口明确后另行接入。
3. 在云端用实拍图验证 YOLO、校准前方有效区域，接通若依到受保护推理服务的链路。
4. 单独评估道路识别、IMU 风险、VLM 解释和真实交通信号数据。

## 当前状态

当前若依后台已接入骑手、设备、定位轨迹、分场景限速、风险事件和私有照片访问；Python 识别服务支持模拟模式及按权重切换的 YOLO 模式。本地启动默认模拟识别，不能据此认定云端模型已经部署或验证。STM32 摄像头 JPEG 和 GPS NMEA 已在实机通过 USB CDC 验证；室内 GPS 暂无有效定位，实际画面较暗，现场识别效果仍需校准。4G 模块、模型权重和地图配置仍需按实物与账号核验。联调步骤见 [`docs/RiderGuard_设备云端联调.md`](docs/RiderGuard_设备云端联调.md)。

## Windows 本地数据库

本机开发环境使用 MySQL 8.4.11 和社区维护的 Redis 8.4.7 Windows 版。程序、下载包和数据放在 `deploy/local-runtime/`，该目录已加入 `.gitignore`。MySQL 只监听本机 3306，Redis 只监听本机 6379；配置与后端 `application-dev.yml` 一致。

首次初始化已导入 `ry_vue.sql`、`ry_workflow.sql`、`ry_job.sql` 和 `ry_ai.sql`。以后运行 `admin/ruoyi/start_riderguard.bat`，或单独运行 `scripts/start_local_services.ps1`，即可拉起本地数据库。若依后端启动后，可以访问 `http://127.0.0.1/dev-api/auth/code` 检查验证码接口。

MySQL 包来自 [Oracle MySQL 官方下载](https://dev.mysql.com/downloads/mysql/)，Redis 包来自 [redis-windows 社区发行页](https://github.com/redis-windows/redis-windows/releases/tag/8.4.7)。Redis 这个 Windows 移植版用于本地开发。

项目级设计文档请查看 [`docs/`](docs/README.md)。


## 从 GitHub 获取后配置

仓库包含管理前后端源码、AI 服务、设备模拟器及项目文档。依赖缓存、本机数据库、模型权重、日志和本地凭证不随仓库上传。

首次运行时，把 `admin/ruoyi/frontend/.env.development.example`、`.env.production.example` 分别复制为同目录下的 `.env.development`、`.env.production`，根据实际后端填写匹配的请求加解密配置。高德 Web Key 放入 `.env.local`，安全密钥按设备云端联调文档配置到后端。本地数据库和设备凭证需要自行初始化。

`admin/ruoyi/frontend` 与 `admin/ruoyi/backend` 的源码已作为普通目录纳入本仓库，无需初始化子模块。原框架许可证保留在对应目录。

## STM32 摄像头与 GPS 实机链路

STM32CubeIDE 工程位于 [device/firmware/stm32f407/](device/firmware/stm32f407/)，Windows USB 网关位于 [device/gateway/](device/gateway/)，wt 服务器部署脚本位于 [deploy/wt/](deploy/wt/)。接线、联动步骤、室内无定位处理和未来 4G 接口迁移见 [STM32/GPS 部署联动](docs/RiderGuard_STM32_GPS_部署联动.md)。
