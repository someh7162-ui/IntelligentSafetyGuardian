"""RiderGuard image inference service.

Run: python -m uvicorn ai.server:app --host 127.0.0.1 --port 8091
Set RIDERGUARD_INFERENCE_MODE=yolo and RIDERGUARD_MODEL_PATH for real inference.
"""

from __future__ import annotations

import os
from functools import lru_cache

from fastapi import FastAPI, Header, HTTPException, Query, Request

app = FastAPI(title="RiderGuard Crowd Inference", version="1.0")


@lru_cache(maxsize=1)
def load_model():
    from ultralytics import YOLO

    path = os.getenv("RIDERGUARD_MODEL_PATH", "")
    if not path:
        raise RuntimeError("RIDERGUARD_MODEL_PATH is required in yolo mode")
    return YOLO(path)


@app.get("/health")
def health():
    return {"status": "ok", "mode": os.getenv("RIDERGUARD_INFERENCE_MODE", "mock")}


@app.post("/infer/crowd")
async def crowd(
    request: Request,
    hint: int | None = Query(default=None, ge=0, le=100),
    x_internal_key: str = Header(default=""),
):
    expected = os.getenv("RIDERGUARD_AI_KEY", "")
    if expected and x_internal_key != expected:
        raise HTTPException(status_code=401, detail="Invalid internal key")
    image = await request.body()
    if len(image) < 4 or len(image) > 1_048_576 or not image.startswith(b"\xff\xd8") or not image.endswith(b"\xff\xd9"):
        raise HTTPException(status_code=400, detail="Expected JPEG <= 1 MB")
    mode = os.getenv("RIDERGUARD_INFERENCE_MODE", "mock").lower()
    if mode == "mock":
        return {"personCount": hint if hint is not None else 0, "mode": "mock"}
    if mode != "yolo":
        raise HTTPException(status_code=503, detail="Unknown inference mode")

    import cv2
    import numpy as np

    frame = cv2.imdecode(np.frombuffer(image, dtype=np.uint8), cv2.IMREAD_COLOR)
    if frame is None:
        raise HTTPException(status_code=400, detail="Could not decode JPEG")
    height, width = frame.shape[:2]
    # Front-road region. Calibrate this polygon to the actual camera mount.
    region = np.array([(0.1 * width, 0.35 * height), (0.9 * width, 0.35 * height),
                       (width, height), (0, height)], dtype=np.int32)
    threshold = float(os.getenv("RIDERGUARD_PERSON_CONFIDENCE", "0.5"))
    result = load_model().predict(frame, classes=[0], conf=threshold, verbose=False)[0]
    count = 0
    for box in result.boxes.xyxy.cpu().numpy():
        x1, y1, x2, y2 = box
        center = (int((x1 + x2) / 2), int((y1 + y2) / 2))
        if cv2.pointPolygonTest(region, center, False) >= 0:
            count += 1
    return {"personCount": count, "mode": "yolo"}
