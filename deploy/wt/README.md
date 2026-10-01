# wt 目录部署源码

这些文件对应当前 `wt/riderguard` 的独立部署：

- `ai_adapter.py`：在本机 8091 接收若依后台图像，使用内部密钥校验，调用本机 18765 的 YOLO 推理服务，只返回置信度至少 0.5 的人数。
- `nginx.conf`：仅监听服务器回环地址 18766，把 `/prod-api/` 代理到若依 18080，并提供管理页面静态文件。
- `start-backend.sh`：用 wt 内 JRE 启动后端，并从服务器本地配置加载数据库等参数。
- `start-all.sh`：按需启动隔离的 MySQL 33306、Redis 16379、YOLO 18765、AI 8091、若依 18080 和 Nginx 18766。

部署目标是 `/home/teach/wt/riderguard`。脚本需要相应的 `runtime/`、`app.jar`、`web/`、`config/application.yml`、`config/redis.conf`、`config/ai-key` 以及已配置的 `/home/teach/wt/yolo_service`。这些均为服务器运行资产或包含凭据，不随 GitHub 提交。需要在新服务器上安装并配置后再运行脚本；不要把服务器上的配置和数据库直接复制进仓库。

脚本仅监听自身的端口；部署和更新前检查端口占用与当前进程，避免中断其他服务。管理页面当前通过 SSH 本地端口转发访问，不能直接作为公网安全配置使用。
