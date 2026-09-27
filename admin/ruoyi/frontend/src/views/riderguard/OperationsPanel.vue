<template>
  <section class="ops-panel">
    <div class="panel-heading"><div><span class="eyebrow">SYSTEM / OPERATIONS</span><h2>设备与 AI 状态</h2></div><el-button :loading="loading" @click="refresh">检查服务</el-button></div>
    <div class="health-grid">
      <div class="health-card"><small>云端识别接口</small><strong :class="health?.status === 'UP' ? 'good' : 'warn'">{{ !health ? '检查中' : health.status === 'UP' ? '可连接' : '不可连接' }}</strong><span>{{ health?.status === 'UP' ? `服务模式 ${health.mode}` : '检查若依到 AI 服务的连接' }}</span></div>
      <div class="health-card"><small>近 1 小时识别成功</small><strong>{{ health?.readyLastHour ?? '—' }}</strong><span>按推理请求统计</span></div>
      <div class="health-card"><small>近 1 小时识别失败</small><strong :class="(health?.failedLastHour || 0) > 0 ? 'warn' : ''">{{ health?.failedLastHour ?? '—' }}</strong><span>失败照片可用相同编号重传</span></div>
      <div class="health-card"><small>平均识别耗时</small><strong>{{ health?.readyLastHour ? health.averageInferenceMs : '—' }} <em v-if="health?.readyLastHour">ms</em></strong><span>近 1 小时成功请求</span></div>
    </div>
    <p class="health-hint">接口可连接仅代表健康接口响应；实际模型效果以实拍照片的识别结果为准。检查时间：{{ health?.checkedAtMs ? formatTime(health.checkedAtMs) : '—' }}</p>
    <div class="sub-heading"><h3>设备链路</h3><span>{{ devices.length }} 台登记设备</span></div>
    <div v-if="!devices.length" class="empty">暂无登记设备</div>
    <div v-else class="device-health-list"><div v-for="device in devices" :key="device.device_id" class="device-health"><div><b>{{ device.rider_name || device.device_id }}</b><small>{{ device.device_id }}</small></div><span :class="deviceState(device).tone">{{ deviceState(device).label }}</span><span :class="device.gps_valid ? 'good' : 'warn'">{{ device.gps_valid ? 'GPS 正常' : 'GPS 无效' }}</span><span :class="device.inference_status === 'ERROR' ? 'warn' : ''">{{ imageState(device) }}</span><small>{{ device.last_seen_ms ? formatTime(device.last_seen_ms) : '从未上报' }}</small></div></div>
    <div class="sub-heading"><h3>近 7 天风险趋势</h3><span>按事件采集日期统计</span></div>
    <div v-if="!analytics?.dailyEvents.length" class="empty">近 7 天暂无风险事件</div>
    <div v-else class="trend"><div v-for="day in analytics.dailyEvents" :key="day.day" class="trend-day"><span>{{ day.events }} 起</span><div class="trend-track"><i :style="{ height: `${Math.max(7, day.events / maxEvents * 100)}%` }"></i></div><small>{{ day.day.slice(5) }}</small></div></div>
  </section>
</template>

<script setup lang="ts">
import { getAiHealth, getAnalytics } from '@/api/riderguard';
import type { AiHealth, Analytics, Device } from '@/api/riderguard';

defineProps<{ devices: Device[] }>();
const health = ref<AiHealth>();
const analytics = ref<Analytics>();
const loading = ref(false);
const maxEvents = computed(() => Math.max(1, ...(analytics.value?.dailyEvents.map(day => day.events) || [])));
const formatTime = (ms: number) => new Date(ms).toLocaleString('zh-CN', { hour12: false });
const deviceState = (device: Device) => {
  if (!device.last_seen_ms || Date.now() - device.last_seen_ms >= 15000) return { label: '离线', tone: 'muted' };
  if (!device.last_sample_ms || Date.now() - device.last_sample_ms >= 15000) return { label: '数据过期', tone: 'warn' };
  return { label: '在线', tone: 'good' };
};
const imageState = (device: Device) => {
  if (!device.latest_image_captured_ms) return '尚未传图';
  if (device.inference_status === 'ERROR') return '识别失败';
  if (Date.now() - device.latest_image_captured_ms > 15000) return '图片过期';
  return device.inference_status === 'READY' ? `识别成功 · ${device.person_count ?? '—'} 人` : '识别中';
};
async function refresh() {
  loading.value = true;
  try {
    const [ai, trend] = await Promise.all([getAiHealth(), getAnalytics()]);
    health.value = ai.data;
    analytics.value = trend.data;
  } finally { loading.value = false; }
}
onMounted(() => { void refresh(); });
</script>

<style scoped lang="scss">
.ops-panel { display: grid; gap: 17px; min-width: 0; padding: 24px; border: 1px solid #e4eae8; border-radius: 16px; background: #fff; box-shadow: 0 8px 28px #1a2b3210; }
.panel-heading, .sub-heading { display: flex; align-items: center; justify-content: space-between; gap: 12px; }.eyebrow { color: #c85d31; font-size: 10px; font-weight: 800; letter-spacing: .15em; }h2 { margin: 5px 0 0; color: #24343c; font-size: 20px; }h3 { margin: 0; color: #273942; font-size: 14px; }.sub-heading { padding-top: 8px; border-top: 1px solid #e8edeb; }.sub-heading span, .health-hint { color: #8b999e; font-size: 11px; }
.health-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 9px; }.health-card { display: grid; gap: 8px; padding: 15px; border-radius: 10px; background: #f4f7f6; }.health-card small, .health-card span { color: #87959b; font-size: 10px; }.health-card strong { color: #263943; font-size: 23px; line-height: 1.2; }.health-card strong em { color: #89989d; font-size: 11px; font-style: normal; font-weight: 500; }.health-card strong.good, .good { color: #2f9767; }.health-card strong.warn, .warn { color: #cf6e3b; }.muted { color: #9ba7ab; }.health-hint { margin: 0; line-height: 1.6; }
.device-health-list { display: grid; max-height: 320px; overflow: auto; }.device-health { display: grid; grid-template-columns: minmax(110px, 1fr) 75px 85px minmax(115px, 1fr) 125px; align-items: center; gap: 8px; min-height: 53px; border-top: 1px solid #ecf0ee; font-size: 11px; }.device-health > div { display: grid; gap: 3px; }.device-health b { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }.device-health small { color: #96a3a6; }.empty { padding: 30px; border: 1px dashed #dbe4e0; border-radius: 9px; color: #87969b; text-align: center; font-size: 12px; }
.trend { display: flex; align-items: end; gap: 14px; min-height: 140px; }.trend-day { display: grid; justify-items: center; gap: 5px; height: 140px; width: 42px; color: #6e7e83; font-size: 10px; }.trend-track { display: flex; align-items: end; width: 24px; height: 103px; border-radius: 4px; background: #f0f3f1; overflow: hidden; }.trend-track i { width: 100%; border-radius: 4px; background: linear-gradient(#f69a6c, #df6838); }.trend-day small { white-space: nowrap; }
@media (max-width: 850px) { .device-health { grid-template-columns: minmax(100px, 1fr) 60px 75px; }.device-health > span:nth-of-type(3), .device-health > small { display: none; } }
@media (max-width: 560px) { .health-grid { grid-template-columns: 1fr; }.ops-panel { padding: 18px; } }
@media (max-width: 992px) {
  .panel-heading, .sub-heading { flex-wrap: wrap; }
  .device-health { grid-template-columns: minmax(0, 1fr) auto; padding: 12px 0; gap: 9px; }
  .device-health > span:nth-of-type(3), .device-health > small { display: block; }
  .device-health > small { grid-column: 1 / -1; }
  .trend { gap: 6px; }.trend-day { flex: 1; min-width: 0; width: auto; }.trend-track { width: min(24px, 100%); }
  .health-card, .device-health { overflow-wrap: anywhere; }
}
</style>
