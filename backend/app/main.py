from fastapi import FastAPI

app = FastAPI(title="Safety Guardian API", version="0.1.0")


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok", "service": "backend"}


@app.get("/api/v1/overview")
def overview() -> dict[str, int]:
    """临时演示接口，后续替换为数据库统计。"""
    return {"onlineDevices": 0, "activeRiders": 0, "todayRiskEvents": 0}

