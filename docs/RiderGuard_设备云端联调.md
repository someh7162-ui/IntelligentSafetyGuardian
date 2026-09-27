# RiderGuard 设备—云端联调

## 本地启动

运行 `admin/ruoyi/start_riderguard.bat`。脚本启动 MySQL、Redis、若依、Vue 和本机模拟识别服务（8091，`mock` 模式）。后台首页可创建骑手和设备。创建设备时只显示一次 64 位设备凭证；请保存到本地安全位置，不要提交到 Git。

若 8080 被无响应的旧进程占用，启动脚本会把新后端放到 18080，并让 Vite 代理自动指向它；若 18080 也被占用，脚本会明确报错。后端每次使用独立的构建文件名，避免运行中的旧 JAR 锁住下一次打包。

从项目根目录运行：

```powershell
python -m pip install -r device/requirements.txt
python device/simulator.py --device-id SG-001 --token <新建时显示的凭证> --seconds 90
```

模拟器每秒上报位置与车速，每 4 秒上传 JPEG；演示人群密集、限速切换、蜂鸣器/状态灯状态和事件记录。断网时数据暂存在 `device/.queue/`，恢复后按原采样编号重传。`mock` 图片结果会在事件列表中标记。这里的 25/10 km/h 是联调值，不是正式安全标准。

## 设备接口

所有设备请求必须走 HTTPS（本地回环测试可用 HTTP），带 `X-Device-Token`；后台仅保存凭证的 SHA-256 值。设备 ID 由管理员创建并绑定骑手。

`POST /device/riderguard/telemetry`，JSON：

```json
{"deviceId":"SG-001","sampleId":"fix-0001","capturedAtMs":1790000000000,"latitude":34.2304,"longitude":108.9342,"speedKph":14.0,"gpsValid":true}
```

响应包含 `mode` (`NORMAL`/`CROWD`)、`limitKph`、`validUntilMs`、`visionStatus`、`overspeed`、`alert` 和 `imageIntervalMs`。STM32 应在本地每次测速后比较当前速度和最后一次收到的限速，不依赖网络往返完成蜂鸣器和灯光提醒。定位无效时 `gpsValid=false`，经纬度传 `null`；后台保存轨迹但不在地图上绘制该点。

`POST /device/riderguard/image`，multipart 字段为 `deviceId`、`sampleId`、`capturedAtMs`、`image`，只接受不超过 1 MB 的完整 JPEG。每个设备内 `sampleId` 唯一，重试不会重复写入图片或轨迹。图片写入服务端私有目录；`GET /riderguard/images/{id}` 只对已登录且具有 `superadmin`、`riderguard_manager` 或 `riderguard_viewer` 角色的用户开放。具有 `superadmin` 或 `riderguard_manager` 角色的用户可以创建设备、绑定骑手、配置限速和处置事件；查看角色只读。

## 识别与地图

云端 Python 服务的 `POST /infer/crowd` 接收 JPEG；本地默认为 `mock`。真实部署设置 `RIDERGUARD_INFERENCE_MODE=yolo`、`RIDERGUARD_MODEL_PATH=<已验证的模型权重>`、`RIDERGUARD_AI_KEY=<内部密钥>`，并在若依进程设置同一 `RIDERGUARD_AI_KEY` 与内部服务地址 `RIDERGUARD_AI_URL`。真实 YOLO 仅计算前方道路区域内的 person 检测框中心；区域多边形需按实车机位校准。连续两张达到人数阈值进入密集模式，连续三张低于阈值退出。推理失败的图片标记 `ERROR`，同一采样编号可重试，不会把失败当作“无人群”。

前端在 `admin/ruoyi/frontend/.env.local` 设置 `VITE_AMAP_KEY=<Web端 JS API Key>` 后展示高德地图。高德安全代理必须位于站点根路径 `/_AMapService`，不能放到 `/dev-api` 或 `/prod-api` 下面；本地 Vite 和生产 Nginx 都把该路径转发给若依后端。如需单独部署代理，可覆盖 `VITE_AMAP_SERVICE_HOST`，但其路径仍须是根路径 `/_AMapService`。若依后端从 `RIDERGUARD_AMAP_SECURITY_FILE` 指定的文件读取安全密钥，并转发到高德固定域名；本地启动脚本默认读取 `deploy/local-runtime/amap-security-code.txt`。部署时应配置同名环境变量，安全密钥不要放进前端环境变量。服务端保存 GNSS 原始经纬度，前端显示时转换为高德坐标。没有 Key 时，定位列表、事件和轨迹点数仍可用。接入依据：[高德 JS API 安全密钥使用说明](https://lbs.amap.com/api/javascript-api-v2/guide/abc/jscode)。

运营首页约每 2 秒读取一次设备最新位置；点击地图标记或设备列表可查看该骑手近 15 分钟或近 1 小时的轨迹、起终点、最新上报时间和车速。轨迹首次加载后增量拉取新点，每 30 秒重新同步一次完整时间窗，以覆盖设备断线后补传的历史点。当前为近实时轮询，页面关闭不影响后端继续记录；GNSS 无效的点保存在轨迹表中，但不会绘制到地图。地图用高德 `AMap.convertFrom(..., 'gps')` 转换原始 GPS 坐标，单次至多 40 对坐标；路线为保持响应速度最多抽样绘制约 500 点，面板中的有效轨迹点数仍统计所选时段的全部有效上报点。

## 管理工作区

若依侧栏中的「骑手安全」分为总览、实时态势、地图与轨迹、风险处置、骑手与设备、规则与运维。「地图与轨迹」是独立的大地图工作区，桌面端地图占主要区域，右侧显示选中骑手的轨迹与设备列表；支持近 15 分钟/近 1 小时轨迹、播放点与车速曲线。风险处置中的「查看地图与轨迹」直接打开该骑手位置。风险处置展示最近 200 条事件，可查看照片、位置和操作记录；经理或超级管理员填写说明后将事件设为核实中、已处置或误报，已结束事件不能再次更改。规则与运维显示 AI 健康接口、最近一小时推理成功/失败与平均耗时、设备上报和图片状态，以及近七天风险趋势。AI 健康接口可连接只表示服务响应，需用实拍图片核验模型效果。

数据库在若依启动时增量创建 `rg_event_action`（事件操作记录）和 `rg_inference_attempt`（推理请求记录）。若当前数据库还没有 `riderguard_manager`、`riderguard_viewer` 角色，可在若依「系统管理 → 角色管理」中创建对应角色键并分配用户。原有超级管理员角色键为 `superadmin`。

## 硬件接入条件

当前仓库有独立的本地限速提醒 C 模块 `device/firmware/riderguard_alert.c`，但没有针对某块 STM32 板卡的完整固件。采购前须确定 4G 板确切型号与 UART/供电规格、OV2640 DCMI 引脚、蜂鸣器与状态灯 GPIO、电池保护和离线存储；参见《外卖安全维护系统_硬件方案可行性评审》后再接线。

## 本地回归检查

启动服务后运行 `python scripts/smoke_riderguard.py --base-url http://127.0.0.1:8080`。脚本会临时创建设备，验证凭证拒绝、正常速度、两张照片切换限速、超速事件与照片、重复上报、无效 GPS、断网队列与补传，并在结束时清理自己生成的记录和图片。要同时检查管理接口，可在当前终端设置 `RIDERGUARD_TEST_ADMIN_PASSWORD` 后重跑；测试包含验证码登录、轨迹与证据读取、事件处置审计和 AI 健康接口。测试机需安装 `device/requirements.txt` 中的依赖，以及 `pymysql`、`redis`、`pycryptodome`。

AI 超时与重试测试需将单独启动的若依进程配置为 `RIDERGUARD_AI_URL=http://127.0.0.1:18091`，再运行 `python scripts/smoke_riderguard.py --base-url http://127.0.0.1:<该进程端口> --fault-ai-port 18091`。脚本会在该端口提供一次慢响应并恢复，验证失败状态、同编号重试和人数状态恢复。

## 地图复盘与轨迹连续性

从总览风险事件或风险处置页打开地图时，查询固定的事发前后各 5 分钟轨迹，并显示可点击的红色事发标记。右侧明确区分事件时间窗与当前设备状态；点击“近 15 分钟”或“近 1 小时”恢复实时轨迹。历史轨迹请求失败时保留已有结果并提供重试。

轨迹按采集时间排序。无效 GPS 点、超过 15 秒的采集间隔、相邻点位移超过 `max(40米, 间隔秒数 × 100km/h)` 时断开路线，不修改原始数据或风险判定。100km/h 仅是地图跳点显示的容错参数，需要按实车定位质量复核。分段使用高德多段折线，不跨断点连线。

实时标记在 1.2 秒内插值移动；过期数据、超过 80 米的位移以及减少动态效果偏好下直接定位。插值仅用于视觉呈现。回放使用采集时间间隔，支持暂停、拖动和 1/10/30/60 倍速；定位缺失时停留在上一有效点，不模拟缺失路段。页面隐藏时暂停轮询与回放，回到页面立即刷新。
