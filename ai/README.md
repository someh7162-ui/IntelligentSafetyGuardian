# AI

现行方案由独立云端 Python 服务运行 YOLO 行人检测，STM32 不运行视觉模型。道路识别与事件后视觉大模型解释是后续阶段；详见 [当前实施方案](../docs/RiderGuard_当前实施方案.md)。模型权重放在 `model_weights/`（不提交到 Git），训练数据放在 `datasets/`（不提交到 Git）。本地服务默认 `mock`，真实 `yolo` 模式需权重和实拍验证。

