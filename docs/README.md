# RiderGuard Project Documentation

This directory contains the project-level design and planning documents.

**Current source of truth:** [RiderGuard 当前实施方案](RiderGuard_当前实施方案.md). STM32 collects data and alerts locally; cloud Python runs YOLO; RuoYi manages telemetry, trajectories, events and review. Older board-side AI and fixed 2–3 second photo plans are canceled.

## Documents

- `RiderGuard_当前实施方案.md` — current architecture, scope, phases and acceptance baseline
- `RiderGuard_设备云端联调.md` — current device protocol and integration
- `后台管理系统详细设计.md` — historical management requirements draft; current interfaces are in the integration document
- `管理系统框架选型.md` — RuoYi-Vue-Plus framework decision
- `RiderGuard界面设计方案.md` — brand, color, animation, and interaction rules
- `安全守护盒硬件设计方案.md` — canceled historical edge-computing option; not an implementation plan
- `外卖安全维护系统_硬件配件采购与连接方案.md` — current STM32 V1 procurement and wiring plan
- `外卖安全维护系统_硬件方案可行性评审.md` — hardware feasibility review
- `AI算法方案.md` — current cloud-first AI stages
- `PROJECT_PLAN.md` — implementation roadmap

The root `README.md` remains the project entry point. Change logs remain in `logs/` so development history stays separate from design documents. Framework-specific READMEs remain inside their own modules.


- [STM32 摄像头/GPS 与管理后台联动](RiderGuard_STM32_GPS_部署联动.md) — 当前 USB 网关、wt 部署和 4G 迁移接口

