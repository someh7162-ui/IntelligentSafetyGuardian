# 正式管理系统

所有 RuoYi 框架内容统一放在 `admin/ruoyi/` 下：后端工程位于 `admin/ruoyi/backend/`，官方前端工程位于 `admin/ruoyi/frontend/`。正式后台页面和 Java 业务模块都在这里开发；根目录的 `frontend/` 仅保留当前的轻量原型。

## 上游版本

- 来源：`https://gitee.com/dromara/RuoYi-Vue-Plus.git`
- 导入提交：`de394a0e21f1e0648ef229d027c3a927d98200a2`
- 导入方式：浅克隆（`--depth 1`）

前端单独导入到 `admin/ruoyi/frontend/`：

- 来源：`https://gitee.com/JavaLionLi/plus-ui.git`
- 导入提交：`4cf4c57e6e36aa91725c0e748919d10861e352a3`
- 版本：`6.0.0`
- 包管理器：pnpm 10，Node.js >= 20.19

## 导入后需要适配的模块

- 设备管理：设备状态、电量、GPS、摄像头、固件版本
- 骑手管理：骑手档案、设备绑定、启停状态
- 轨迹管理：定位点、速度、轨迹回放
- 风险事件：风险等级、图片、位置、处置状态
- AI 报告：分析原因、建议、模型版本、置信度
- 实时通信：MQTT 消费、WebSocket 推送

## 一键启动

双击项目根目录的 `start_riderguard.bat`，或双击本目录的 `start_riderguard.bat`。

脚本会分别打开后端和前端窗口：

- 前端：`http://localhost:5173`
- 后端：`http://localhost:8080`
- 后端使用 Maven `dev` 配置和 JDK 21。
- 启动脚本会优先使用 `JAVA_HOME`；当前电脑已配置 Microsoft OpenJDK 21。
- 首次运行会自动执行 `pnpm install --frozen-lockfile`。

后端默认依赖本机 MySQL `3306`（数据库 `ry-vue`）和 Redis `6379`。如果数据库或 Redis 尚未启动，前端窗口仍会打开，但登录和接口请求无法正常工作。

## 导入方式

将指定版本的 RuoYi-Vue-Plus 源码放入本目录，并在本文件中补充上游版本、提交号和来源地址。不要直接修改框架通用模块，业务代码放在项目自己的业务包和前端业务目录中。
