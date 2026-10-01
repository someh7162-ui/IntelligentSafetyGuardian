# wt 目录部署源码

这些文件对应当前 `wt/riderguard` 的独立部署：

- `ai_adapter.py`：在本机 8091 接收若依后台图像，使用内部密钥校验，调用本机 18765 的 YOLO 推理服务，只返回置信度至少 0.5 的人数。
- `nginx.conf`：仅监听服务器回环地址 18766，把 `/prod-api/` 代理到若依 18080，并提供管理页面静态文件。
- `start-backend.sh`：用 wt 内 JRE 启动后端，并从服务器本地配置加载数据库等参数。
- `start-all.sh`：按需启动隔离的 MySQL 33306、Redis 16379、AI 8091、若依 18080 和 Nginx 18766。YOLO 18765 需先检查显卡负载并单独启动；脚本不会自动拉起占用 GPU 的进程。

部署目标是 `/home/teach/wt/riderguard`。脚本需要相应的 `runtime/`、`app.jar`、`web/`、`config/application.yml`、`config/redis.conf`、`config/ai-key` 以及已配置的 `/home/teach/wt/yolo_service`。这些均为服务器运行资产或包含凭据，不随 GitHub 提交。需要在新服务器上安装并配置后再运行脚本；不要把服务器上的配置和数据库直接复制进仓库。

脚本仅监听自身的端口；部署和更新前检查端口占用与当前进程，避免中断其他服务。管理页面当前通过 SSH 本地端口转发访问，不能直接作为公网安全配置使用。

AI 适配器默认最多允许 1 个同时进行的 YOLO 推理（`RIDERGUARD_AI_MAX_INFLIGHT`，允许 1～4）；忙时返回 HTTP 429。Windows 网关在重试期间若图片拍摄已超过 15 秒，会丢弃该帧，避免陈旧结果改变实时风险状态。后端也只让按拍摄时间顺序到达且足够新的图片更新人群风险；历史图片仍可保存和查看。

`start-backend.sh` 启用图片清理：每 15 分钟分批清理拍摄超过 3 天、且没有风险事件关联的普通图片及对应推理记录。风险事件关联的照片保留。可用 `RIDERGUARD_IMAGE_CLEANUP_ENABLED=false` 停用清理；默认批次为 500，可通过 `RIDERGUARD_IMAGE_CLEANUP_BATCH_SIZE` 设置 1～1000。首次清理在后端启动约 5 分钟后运行。
