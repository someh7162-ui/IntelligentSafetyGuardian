# RiderGuard 模拟红绿灯联调

本功能只在若依进程设置 `RIDERGUARD_DEMO_MODE=true` 时启用。`admin/ruoyi/start_riderguard.bat` 的演示启动流程已设置该变量；默认关闭。DEMO-001 位于原始 GPS 坐标 `34.2313,108.93555`，只模拟东北向直行接近，地图展示前按现有逻辑转换为高德坐标。

## 设备协议

`POST /device/riderguard/telemetry` 在原有字段之外可传 `heading`（0～小于 360 度）和 `gpsAccuracy`（米）。旧设备可不传，轨迹仍会记录，但不会产生模拟红灯提醒。响应保留原有布尔字段 `alert` 的超速语义，新增 `alerts.trafficSignal`、`alerts.overspeed` 和 `trafficSignal`。设备应以 `trafficSignal.validUntilMs` 判断信号是否过期；过期或未知时不应作出红灯确认。原有 `validUntilMs` 仍属于限速策略。

模拟器路线在约第 17～73 秒进入测试路口 100 米范围；运行至少 90 秒可观察灯色、倒计时、提醒与风险事件。周期为红 30 秒、绿 25 秒、黄 5 秒；实际触发时间随运行时刻变化。运行示例：

```powershell
python device/simulator.py --device-id SG-001 --token <设备凭证> --seconds 90
```

地图接口 `GET /riderguard/traffic-signal/demo` 仅向已登录且具有 RiderGuard 角色的后台用户返回模拟路口状态。前端每次刷新明确标记“模拟信号 / MOCK”。后端只在定位有效、精度不大于 25 米、采集时间距服务器时间不超过 5 秒、位于路口前方 5～100 米且航向与路口方位偏差不大于 45 度时返回模拟灯色。移动速度达到 3 km/h 且处于红灯时才触发提醒。红灯提醒存入 `rg_event` 和 `rg_traffic_signal_event`，按同一设备与路口限频；重复采样编号不会再生成事件。

若依启动时通过 `RiderGuardSchema` 升级现有 RiderGuard 表；`rg_schema_migration` 记录已完成的版本：1 为基础表基线，2 为轨迹航向、红灯事件字段和 `(track_id,event_type)` 唯一约束。MySQL DDL 会隐式提交，版本 2 的每一步均先检查数据库元数据；启动中断后再次启动会补齐未完成的步骤，全部成功后才写入版本记录。生产环境升级前仍应备份数据库，并在升级后查询 `SELECT version,description,applied_at FROM rg_schema_migration ORDER BY version` 核验。

本地联调可运行 `python scripts/smoke_riderguard.py --traffic-demo`；加入 `--admin-password <本地管理员密码>` 时还会检查管理端的事件详情、照片访问和处置记录。风险事件页提供事件类型及状态筛选，筛选范围为最近 200 条已加载事件。真实路口灯色、精确倒计时、手机导航 SDK 桥接及 STM32 实机 GPIO 验证不属于此模拟功能。
