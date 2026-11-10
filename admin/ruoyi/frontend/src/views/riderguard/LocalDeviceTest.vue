<template>
  <section class="local-test">
    <div class="test-heading"><div><span class="eyebrow">LOCAL / USB</span><h3>本机设备接入</h3></div><el-tag :type="state.connected ? 'success' : 'info'">{{ state.connected ? 'USB 已连接' : state.running ? '连接中' : '未运行' }}</el-tag></div>
    <p class="hint">开发板 USB → 本机网关 → 正式设备接口。定位、照片直接进入骑手地图和摄像头页面。</p>
    <el-alert v-if="!state.available" title="当前服务未启用本机 USB 控制" description="云端服务无法直接访问电脑串口。硬件通过 Windows 网关上传后，可在骑手地图和摄像头页面查看。" type="info" :closable="false" />
    <div v-else class="connection-form">
      <label>登记设备<el-select v-model="deviceId" placeholder="选择已创建的设备" :disabled="state.running"><el-option v-for="device in devices" :key="device.device_id" :value="device.device_id" :label="`${device.rider_name || device.device_id} · ${device.device_id}`" /></el-select></label>
      <label>USB 串口<div class="port-field"><el-select v-model="port" placeholder="选择串口" :disabled="state.running"><el-option v-for="item in ports" :key="item.port" :value="item.port" :label="`${item.port} · ${item.description}`" /></el-select><el-button :disabled="state.running || busy" @click="loadPorts">扫描</el-button></div></label>
      <label class="token-field">设备凭证<el-input v-model="token" type="password" show-password autocomplete="off" placeholder="粘贴该设备的 64 位凭证，仅用于本次连接" :disabled="state.running" maxlength="64" /></label>
      <div class="test-actions"><el-button v-if="!state.running" type="primary" :loading="busy" :disabled="!deviceId || !port || !validToken" @click="start">开始 USB 测试</el-button><el-button v-else type="danger" plain :loading="busy" @click="stop">停止并释放串口</el-button><el-button :disabled="!state.connected || busy" @click="command('capture')">上传一张照片</el-button><el-switch :model-value="state.autoImages" :disabled="!state.connected || busy" active-text="自动传图" @change="value => command('images', !!value)" /></div>
      <p class="hint">定位目标 {{ state.telemetryIntervalMs / 1000 }} 秒；自动传图 {{ state.imageIntervalMs / 1000 }} 秒。开始连接时自动传图保持关闭，可先验证定位，再手动传图。</p>
      <p v-if="!ports.length && !state.running" class="hint">当前未检测到 USB 串口。连接开发板的数据线后点击“扫描”。</p>
    </div>
    <p v-if="error" class="test-error" role="alert">{{ error }}</p>
    <div v-if="state.startedAtMs" class="test-readings">
      <div><small>正在接入</small><b>{{ state.deviceId }} / {{ state.port }}</b></div>
      <div><small>GPS 状态</small><b>{{ !state.running ? '测试已停止' : state.telemetry?.sampleId && !telemetryFresh ? '数据已过期' : state.telemetry?.gpsValid === true ? '有效定位' : state.telemetry?.sampleId ? '尚未定位' : '等待 NMEA' }}</b></div>
      <div><small>GPS 速度</small><b>{{ telemetryFresh && state.telemetry?.gpsValid === true ? Number(state.telemetry.speedKph).toFixed(1) + ' km/h' : '—' }}</b></div>
      <div><small>后台限速</small><b>{{ state.policy?.limitKph ? state.policy.limitKph + ' km/h' : '等待响应' }}</b></div>
      <div><small>最近照片</small><b>{{ state.image?.status || '尚未上传' }}<template v-if="state.image?.personCount != null"> · {{ state.image.personCount }} 人</template></b></div>
      <div><small>视觉状态</small><b>{{ state.policy?.visionStatus === 'READY' ? '最近识别有效' : state.policy?.visionStatus === 'STALE' ? '等待新照片' : '—' }}</b></div>
    </div>
    <div v-if="state.deviceId" class="test-links"><router-link :to="{ path: '/riderguard/map', query: { device: state.deviceId } }">查看地图与轨迹 ↗</router-link><router-link :to="{ path: '/riderguard/camera', query: { device: state.deviceId } }">查看摄像头 ↗</router-link></div>
    <p class="hint">本机网关只读取 USB 数据。后台策略已在此显示；实物蜂鸣器/状态灯的执行回调仍需按接线并入固件。</p>
    <details v-if="state.events.length" class="test-events"><summary>接入记录</summary><ol><li v-for="event in state.events" :key="event.id" :class="event.kind"><time>{{ formatTime(event.timeMs) }}</time><span>{{ event.message }}</span></li></ol></details>
  </section>
</template>

<script setup lang="ts">
import { getLocalDeviceStatus, getLocalDevicePorts, startLocalDevice, stopLocalDevice, commandLocalDevice } from '@/api/riderguard';
import type { Device, LocalDeviceState, LocalDevicePort } from '@/api/riderguard';

defineProps<{ devices: Device[] }>();
const state = ref<LocalDeviceState>({ available: false, running: false, connected: false, deviceId: '', port: '', autoImages: false, startedAtMs: 0, telemetryIntervalMs: 3000, imageIntervalMs: 30000, visionStaleAfterMs: 90000, events: [] });
const deviceId = ref(''), port = ref(''), token = ref(''), error = ref(''), busy = ref(false);
const ports = ref<LocalDevicePort[]>([]);
const validToken = computed(() => /^[0-9a-f]{64}$/.test(token.value));
const checkedAt = ref(Date.now());
const telemetryFresh = computed(() => state.value.running && state.value.connected && !!state.value.telemetry?.capturedAtMs && checkedAt.value - state.value.telemetry.capturedAtMs < 15000);
const formatTime = (ms: number) => new Date(ms).toLocaleTimeString('zh-CN', { hour12: false });
let timer: ReturnType<typeof setInterval> | undefined;
let disposed = false, refreshing = false;

async function refresh() {
  if (refreshing || disposed) return;
  refreshing = true;
  try {
    const result = await getLocalDeviceStatus();
    if (disposed) return;
    state.value = result.data;
    checkedAt.value = Date.now();
    if (state.value.running) { deviceId.value = state.value.deviceId; port.value = state.value.port; }
  } catch { if (!disposed) error.value = '本机接入状态读取失败，请检查后台连接'; }
  finally { refreshing = false; }
}
async function loadPorts() {
  try { ports.value = (await getLocalDevicePorts()).data || []; if (!port.value && ports.value.length === 1) port.value = ports.value[0].port; }
  catch { error.value = '无法扫描串口，请检查 USB 网关是否已安装'; }
}
async function perform(action: () => Promise<unknown>) {
  if (busy.value) return;
  busy.value = true; error.value = '';
  try { await action(); await refresh(); }
  catch { error.value = '操作失败，请核对设备凭证、串口和后台提示'; }
  finally { busy.value = false; }
}
async function start() {
  await perform(async () => { await startLocalDevice(deviceId.value, port.value, token.value); token.value = ''; });
}
async function stop() { await perform(async () => { await stopLocalDevice(); token.value = ''; }); }
async function command(action: 'capture' | 'images', enabled?: boolean) { await perform(() => commandLocalDevice(action, enabled)); }
onMounted(async () => { await refresh(); if (state.value.available) await loadPorts(); timer = setInterval(() => { if (!document.hidden) void refresh(); }, 3000); });
onUnmounted(() => { disposed = true; token.value = ''; if (timer) clearInterval(timer); });
</script>

<style scoped lang="scss">
.local-test { display: grid; gap: 15px; padding-top: 22px; border-top: 1px solid #e8edeb; }
.test-heading, .test-actions, .port-field, .test-links { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
.test-heading { justify-content: space-between; }h3 { margin: 5px 0 0; font-size: 17px; color: #263943; }.eyebrow { color: #c85d31; font-size: 10px; font-weight: 800; letter-spacing: .15em; }
.hint { margin: 0; color: #7c8c93; font-size: 12px; line-height: 1.8; }.connection-form { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }.connection-form label { display: grid; gap: 8px; color: #53676f; font-size: 12px; }.token-field, .test-actions, .connection-form > .hint { grid-column: 1 / -1; }.port-field { flex-wrap: nowrap; }.port-field .el-select { min-width: 0; flex: 1; }.test-error { padding: 10px 14px; border-radius: 6px; background: #fff3ef; color: #ad4d2b; font-size: 12px; }
.test-readings { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 10px; }.test-readings > div { display: grid; gap: 6px; padding: 13px; border-radius: 8px; background: #f4f7f6; }.test-readings small { color: #87959b; font-size: 11px; }.test-readings b { color: #354e58; font-size: 13px; overflow-wrap: anywhere; }.test-links a { color: #bc5b32; font-size: 12px; }.test-events { font-size: 12px; color: #657a82; }.test-events summary { cursor: pointer; }.test-events ol { margin: 12px 0 0; padding: 0; list-style: none; max-height: 240px; overflow: auto; }.test-events li { display: flex; gap: 12px; padding: 8px 0; border-top: 1px solid #edf1ee; }.test-events time { color: #92a0a5; white-space: nowrap; }.test-events .error, .test-events .warning { color: #bd6038; }
@media(max-width: 680px) { .connection-form { grid-template-columns: 1fr; }.test-readings { grid-template-columns: 1fr 1fr; }.test-actions { gap: 10px; } }
</style>
