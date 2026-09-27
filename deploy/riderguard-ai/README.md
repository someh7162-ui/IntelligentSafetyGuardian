# RiderGuard 云端推理服务

此目录复制到服务器 `/home/wt/riderguard-ai/`。服务只监听 `127.0.0.1:8091`，不更改系统 Python、Nginx、Docker 或其他服务。

- `./start.sh`：启动服务；`./stop.sh`：停止服务。
- `curl http://127.0.0.1:8091/health`：检查 HTTP 服务状态。该检查不会加载模型；正式验证还需提交 JPEG 到 `/infer/crowd`。
- `.env`：服务配置与内部密钥，权限应为 `600`；不要提交到代码仓库。
- `model_weights/`：预训练权重；`logs/`：安装和服务日志。

联调时，可在运行若依的计算机上用 SSH 本地转发连接服务器的 8091，然后把若依的 `RIDERGUARD_AI_URL` 设置为 `http://127.0.0.1:18091`，并配置相同的 `RIDERGUARD_AI_KEY`。SSH 转发断开时，若依会把识别结果标记为失败，设备照片仍保存在若依端。生产环境需要持久且受保护的内网连接。
