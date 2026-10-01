<template>
  <main class="camera-monitor">
    <header class="page-header">
      <div>
        <span class="eyebrow">RIDER CAMERA</span>
        <h1>骑手实时画面</h1>
        <p>选择骑手查看设备最近上传的照片。页面每 2 秒检查新帧，当前 USB 网关约每 4 秒上传一张 JPEG。</p>
      </div>
      <div class="header-actions">
        <el-switch v-model="autoRefresh" active-text="自动刷新" />
        <el-button :loading="loadingDevices" @click="refresh">立即刷新</el-button>
      </div>
    </header>

    <div class="monitor-grid">
      <aside class="device-panel">
        <div class="panel-title"><h2>骑手与设备</h2><span>{{ devices.length }} 台</span></div>
        <el-input v-model="search" clearable placeholder="搜索骑手或设备编号" aria-label="搜索骑手或设备编号" />
        <div v-if="loadingDevices && !devices.length" class="empty">正在加载设备…</div>
        <div v-else-if="deviceError && !devices.length" class="empty">设备列表暂不可用，请重试。</div>
        <div v-else-if="!filteredDevices.length" class="empty">暂无匹配的设备。</div>
        <div v-else class="device-list">
          <button v-for="device in filteredDevices" :key="device.device_id" type="button" class="device-item"
            :class="{ selected: selectedId === device.device_id }" :aria-pressed="selectedId === device.device_id"
            @click="selectDevice(device.device_id)">
            <span class="device-name"><strong>{{ device.rider_name || device.device_id }}</strong><small>{{ device.device_id }}</small></span>
            <span class="device-badge" :class="{ offline: !isOnline(device) }">{{ isOnline(device) ? '在线' : '离线' }}</span>
          </button>
        </div>
      </aside>

      <section class="viewer-panel" aria-label="骑手摄像头最近画面">
        <template v-if="selectedDevice">
          <div class="viewer-head">
            <div><h2>{{ selectedDevice.rider_name || selectedDevice.device_id }}</h2><p>{{ selectedDevice.device_id }} · {{ isOnline(selectedDevice) ? '设备在线' : '设备离线' }}</p></div>
            <span class="freshness" :class="{ stale: !imageFresh }">{{ imageFresh ? '最新画面' : selectedDevice.latest_image_id ? '画面已过期' : '等待画面' }}</span>
          </div>
          <div class="image-stage">
            <img v-if="frameUrl" :src="frameUrl" :alt="`${selectedDevice.rider_name || selectedDevice.device_id}最近上传的摄像头照片`" />
            <div v-else class="image-placeholder">{{ loadingFrame ? '正在读取照片…' : frameError ? '照片读取失败，可点击立即刷新重试' : '该设备尚未上传照片' }}</div>
            <div v-if="loadingFrame && frameUrl" class="image-loading">正在加载新画面…</div>
          </div>
          <div class="viewer-foot">
            <span>拍摄时间：{{ frameCapturedMs ? formatTime(frameCapturedMs) : '暂无' }}</span>
            <span>距今：{{ frameCapturedMs ? `${Math.max(0, Math.floor((nowMs - frameCapturedMs) / 1000))} 秒` : '—' }}</span>
            <span v-if="frameError" class="error">新画面读取失败，正在显示上一帧</span>
            <span v-if="deviceError" class="error">设备状态更新失败</span>
          </div>
        </template>
        <div v-else class="select-placeholder">从左侧选择一位骑手，查看其摄像头最近上传的画面。</div>
      </section>
    </div>
    <p class="privacy-note">画面仅供有后台权限的人员查看；这是一组持续更新的照片，不能代表连续视频直播。</p>
  </main>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { useRoute } from 'vue-router';
import { getDevices, getEvidenceImage } from '@/api/riderguard';
import type { Device } from '@/api/riderguard';

const route = useRoute();
const devices = ref<Device[]>([]);
const selectedId = ref('');
const search = ref('');
const frameUrl = ref('');
const frameImageId = ref<number>();
const frameCapturedMs = ref<number>();
const loadingDevices = ref(false);
const loadingFrame = ref(false);
const deviceError = ref(false);
const frameError = ref(false);
const autoRefresh = ref(true);
const nowMs = ref(Date.now());
const selectedDevice = computed(() => devices.value.find(device => device.device_id === selectedId.value));
const filteredDevices = computed(() => devices.value.filter(device =>
  `${device.rider_name || ''} ${device.device_id}`.toLowerCase().includes(search.value.trim().toLowerCase())
));
const imageFresh = computed(() => Boolean(frameCapturedMs.value && nowMs.value - frameCapturedMs.value < 15000));
const isOnline = (device: Device) => Boolean(device.last_seen_ms && nowMs.value - device.last_seen_ms < 15000);
const formatTime = (ms: number) => new Date(ms).toLocaleString('zh-CN', { hour12: false });
let interval: ReturnType<typeof setInterval> | undefined;
let frameVersion = 0;
let disposed = false;

function clearFrame() {
  if (frameUrl.value) URL.revokeObjectURL(frameUrl.value);
  frameUrl.value = '';
  frameImageId.value = undefined;
  frameCapturedMs.value = undefined;
}

async function syncFrame() {
  const device = selectedDevice.value;
  const imageId = device?.latest_image_id;
  if (!device || !imageId || imageId === frameImageId.value || loadingFrame.value) return;
  const requestedDevice = device.device_id;
  const requestedCapturedMs = device.latest_image_captured_ms;
  const version = ++frameVersion;
  loadingFrame.value = true;
  frameError.value = false;
  try {
    const blob = await getEvidenceImage(imageId);
    if (disposed || version !== frameVersion || selectedId.value !== requestedDevice) return;
    const nextUrl = URL.createObjectURL(blob);
    clearFrame();
    frameUrl.value = nextUrl;
    frameImageId.value = imageId;
    frameCapturedMs.value = requestedCapturedMs;
  } catch {
    if (version === frameVersion && !disposed) frameError.value = true;
  } finally {
    if (version === frameVersion) loadingFrame.value = false;
  }
}

function selectDevice(id: string) {
  if (selectedId.value === id) return;
  frameVersion++;
  loadingFrame.value = false;
  frameError.value = false;
  clearFrame();
  selectedId.value = id;
  void syncFrame();
}

async function refresh() {
  if (loadingDevices.value || disposed) return;
  loadingDevices.value = true;
  nowMs.value = Date.now();
  try {
    devices.value = (await getDevices()).data || [];
    deviceError.value = false;
    if (selectedId.value && !selectedDevice.value) selectDevice('');
    await syncFrame();
  } catch {
    if (!disposed) deviceError.value = true;
  } finally {
    loadingDevices.value = false;
  }
}

function onVisibilityChange() {
  if (!document.hidden && autoRefresh.value) void refresh();
}
watch(autoRefresh, enabled => { if (enabled) void refresh(); });
onMounted(() => {
  if (typeof route.query.device === 'string') selectedId.value = route.query.device;
  void refresh();
  interval = setInterval(() => {
    nowMs.value = Date.now();
    if (autoRefresh.value && !document.hidden) void refresh();
  }, 2000);
  document.addEventListener('visibilitychange', onVisibilityChange);
});
onUnmounted(() => {
  disposed = true;
  frameVersion++;
  if (interval) clearInterval(interval);
  document.removeEventListener('visibilitychange', onVisibilityChange);
  clearFrame();
});
</script>

<style scoped lang="scss">
.camera-monitor { max-width: 1500px; margin: 0 auto; padding: 28px; color: #182127; }
.page-header { display: flex; justify-content: space-between; gap: 24px; align-items: flex-start; margin-bottom: 22px; }
.page-header h1 { margin: 5px 0 8px; font-size: 30px; }
.page-header p, .viewer-head p, .privacy-note { margin: 0; color: #718087; line-height: 1.6; }
.eyebrow { color: #d85f2a; font-size: 11px; font-weight: 700; letter-spacing: .15em; }
.header-actions { display: flex; align-items: center; gap: 14px; white-space: nowrap; }
.monitor-grid { display: grid; grid-template-columns: minmax(230px, 300px) minmax(0, 1fr); gap: 18px; min-height: 620px; }
.device-panel, .viewer-panel { background: #fff; border: 1px solid #e1e8e6; border-radius: 14px; box-shadow: 0 8px 32px #1d46400a; }
.device-panel { padding: 18px; }
.panel-title, .viewer-head, .viewer-foot { display: flex; justify-content: space-between; align-items: center; gap: 12px; }
.panel-title { margin-bottom: 14px; }.panel-title h2, .viewer-head h2 { margin: 0; font-size: 17px; }.panel-title span { color: #849097; font-size: 13px; }
.device-list { display: grid; gap: 5px; max-height: 680px; overflow-y: auto; margin-top: 12px; }
.device-item { display: flex; align-items: center; justify-content: space-between; gap: 8px; width: 100%; padding: 12px; border: 0; border-radius: 9px; background: transparent; color: inherit; text-align: left; cursor: pointer; }
.device-item:hover, .device-item.selected { background: #eef5f2; }.device-item.selected { outline: 1px solid #83b9a6; }
.device-name { display: grid; gap: 4px; min-width: 0; }.device-name strong, .device-name small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }.device-name small { color: #89969b; font-size: 11px; }
.device-badge { flex: none; color: #278767; font-size: 12px; }.device-badge.offline { color: #9aa5a8; }
.viewer-panel { display: flex; flex-direction: column; padding: 18px; min-width: 0; }.viewer-head { margin-bottom: 16px; }.viewer-head p { margin-top: 5px; font-size: 12px; }
.freshness { padding: 6px 10px; border-radius: 999px; background: #e6f5ed; color: #26845f; font-size: 12px; white-space: nowrap; }.freshness.stale { background: #fff0e8; color: #aa632d; }
.image-stage { position: relative; display: grid; place-items: center; flex: 1; min-height: 430px; background: #12191c; border-radius: 10px; overflow: hidden; }
.image-stage img { display: block; max-width: 100%; max-height: 66vh; object-fit: contain; }.image-placeholder { padding: 20px; color: #c3cbcc; text-align: center; }
.image-loading { position: absolute; right: 12px; bottom: 12px; padding: 7px 10px; border-radius: 6px; background: #192a2bd9; color: #fff; font-size: 12px; }
.viewer-foot { flex-wrap: wrap; justify-content: flex-start; margin-top: 14px; color: #6c7c83; font-size: 12px; }.error { color: #c05b34; }
.empty { padding: 24px 6px; color: #89969b; text-align: center; font-size: 13px; }.select-placeholder { display: grid; place-items: center; flex: 1; color: #829198; text-align: center; padding: 24px; }
.privacy-note { margin-top: 14px; font-size: 12px; }
@media (max-width: 850px) { .camera-monitor { padding: 16px; }.page-header { flex-direction: column; }.monitor-grid { grid-template-columns: 1fr; min-height: 0; }.device-panel { max-height: 240px; overflow-y: auto; }.image-stage { min-height: 260px; } }
</style>
