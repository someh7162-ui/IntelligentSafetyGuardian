<template>
  <main class="rg-console" :class="{ 'map-page': page === 'map' }">
    <header class="rg-heading">
      <div class="heading-copy"><h1>{{ pageHeading }}</h1><p>{{ pageDescription }}</p></div>
      <div class="heading-actions"><span class="live" :class="{ disconnected: backendError || !refreshedAt || Date.now() - refreshedAt > 20000 }"><span class="live-dot"></span>{{ backendError ? '数据连接中断' : !refreshedAt ? '正在连接' : Date.now() - refreshedAt > 20000 ? '数据更新延迟' : '数据已同步' }}</span><span class="updated">{{ refreshedAt ? `更新于 ${time(refreshedAt)}` : '等待最新数据' }}</span><button class="refresh-button" type="button" aria-label="刷新运营数据" :disabled="refreshing" @click="refresh(true)"><span :class="{ spinning: refreshing }">↻</span> 刷新</button></div>
    </header>

    <section v-if="page === 'overview'" class="metrics">
      <article v-for="item in metricItems" :key="item.label" :class="item.tone"><div class="metric-top"><span>{{ item.label }}</span></div><strong>{{ item.value }}<em v-if="item.unit">{{ item.unit }}</em></strong><div class="metric-bottom"><span class="metric-line"></span><small>{{ item.note }}</small></div></article>
    </section>
    <router-link v-if="page === 'overview'" class="risk-entry" to="/riderguard/events"><div><strong>风险事件</strong><span>{{ overview.openEvents }} 起待处置 · 查看证据与处理记录</span></div><b>进入事件中心 →</b></router-link>
    <div v-show="isMapView" class="main-grid" :class="{ 'live-only': page === 'live', 'map-only': page === 'map' }">
      <section ref="mapPanel" class="panel position-panel" :class="{ 'rail-collapsed': railCollapsed, 'sheet-open': mobileSheetOpen }">
        <div class="panel-head"><div><h2>{{ page === 'map' ? '骑手地图' : '实时位置' }} <span class="heading-caption">/ {{ page === 'map' ? '轨迹监测' : '骑行态势' }}</span></h2></div><div class="map-head-actions"><button v-if="page === 'map'" class="map-expand desktop-rail-toggle" type="button" :aria-expanded="!railCollapsed" @click="railCollapsed = !railCollapsed">{{ railCollapsed ? '显示骑手面板' : '收起面板' }}</button><button class="map-expand map-fullscreen" type="button" @click="toggleMapFullscreen">{{ mapFullscreen ? '退出全屏' : '全屏地图' }}</button><span class="tag">{{ devices.length }} 台设备</span><router-link v-if="page !== 'map'" class="map-expand" to="/riderguard/map">打开大地图 ↗</router-link></div></div>
        <div class="map-workspace-body">
        <div class="map-stage">
          <div v-if="mapKey && !mapError" ref="mapElement" class="map"></div>
          <div v-else class="map map-fallback"><span class="fallback-symbol">⌖</span><strong>{{ mapError ? '地图暂时不可用' : '地图等待高德 Key' }}</strong><p>{{ mapError ? '请检查网络及高德服务，定位列表仍可使用。' : '配置高德 Web Key 后显示位置和轨迹。' }}</p><button v-if="mapError && mapKey" class="refresh-button" @click="retryMap">重试地图</button></div>
          <div class="map-overlay map-overlay-top"><span class="map-live-dot"></span> 实时定位 <span class="map-overlay-count">{{ locatedCount }} 个有效定位</span></div>
          <button v-if="demoIntersection?.enabled" class="map-signal-card" type="button" :aria-expanded="signalDetailsOpen" @click="signalDetailsOpen = !signalDetailsOpen">
            <span class="signal-dot" :class="displaySignalState.toLowerCase()"></span>
            <span><b>DEMO-001 · 模拟信号</b><small>{{ signalLabel }}<template v-if="displaySignalState !== 'UNKNOWN'"> · {{ signalRemaining }} 秒</template></small><small v-if="signalDetailsOpen">仅用于联调 · 东北向直行 · {{ demoIntersection.signal?.source }}<br>更新于 {{ demoIntersection.signal ? time(demoIntersection.signal.observedAtMs) : '—' }}</small></span>
          </button>
          <div class="map-overlay map-legend"><span><i class="legend-dot online"></i>在线</span><span><i class="legend-dot crowd"></i>人群密集</span><span><i class="legend-dot offline"></i>离线</span></div>
        </div>
        <div v-show="mobileMap || !railCollapsed || page !== 'map'" class="map-rail">
        <button v-if="mobileMap" class="sheet-handle" type="button" :aria-expanded="mobileSheetOpen" aria-controls="rider-sheet-content" @pointerdown="startSheetGesture" @pointerup="endSheetGesture" @pointercancel="sheetDragged = false" @click="toggleSheet"><i></i><span>{{ selectedDeviceInfo?.rider_name || selectedDevice || `骑手列表 · ${devices.length} 台设备` }}</span><small>{{ mobileSheetOpen ? '收起 ▾' : '展开 ▴' }}</small></button>
        <div v-if="mobileMap && mobileSheetOpen && selectedDevice" class="sheet-tabs" role="group" aria-label="骑手面板内容"><button type="button" :aria-pressed="sheetView === 'detail'" @click="sheetView = 'detail'">骑手详情</button><button type="button" :aria-pressed="sheetView === 'fleet'" @click="sheetView = 'fleet'">切换骑手 · {{ devices.length }}</button></div>
        <div v-show="!mobileMap || mobileSheetOpen" id="rider-sheet-content" ref="sheetContent" class="sheet-content">
        <div v-if="selectedDevice && (!mobileMap || sheetView === 'detail')" class="track-inspector">
          <div class="track-heading"><div><span class="eyebrow">骑手详情</span><h3>{{ selectedDeviceInfo?.rider_name || selectedDevice }}</h3><span class="track-id">{{ selectedDevice }}</span></div><button class="track-close" type="button" aria-label="关闭轨迹" @click="clearSelectedDevice">×</button></div>
          <div class="rider-status-line"><span :class="selectedDeviceInfo && online(selectedDeviceInfo) ? 'status-online' : 'status-offline'">{{ selectedDeviceInfo && online(selectedDeviceInfo) ? '设备在线' : '设备离线' }}</span><button class="map-expand" type="button" :disabled="!selectedDeviceInfo?.gps_valid || !!eventAt" :aria-pressed="followRider" @click="toggleFollow">{{ followRider ? '停止跟随' : '跟随骑手' }}</button></div>
          <div class="rider-limit">规则限速 <b>{{ selectedDeviceInfo?.crowd_mode ? policy.crowd_limit_kph : policy.normal_limit_kph }} km/h</b><span>{{ selectedDeviceInfo?.crowd_mode ? '人群密集' : '普通路段' }}</span></div>
          <div class="track-stats"><div><small>当前车速</small><strong>{{ !selectedDeviceInfo || !telemetryFresh(selectedDeviceInfo) || selectedDeviceInfo.last_speed == null ? '—' : Number(selectedDeviceInfo.last_speed).toFixed(1) }} <em>km/h</em></strong></div><div><small>最新上报</small><strong>{{ selectedDeviceInfo?.last_sample_ms ? time(selectedDeviceInfo.last_sample_ms) : '—' }}</strong></div><div><small>有效轨迹点</small><strong>{{ validTracks.length }} <em>个</em></strong></div></div>
          <div class="rider-photo"><button v-if="riderPhotoUrl" type="button" @click="viewImage(selectedDeviceInfo!.latest_image_id!)"><img :src="riderPhotoUrl" alt="骑手最近一次前方现场照片" /></button><span v-else>{{ riderPhotoLoading ? '正在加载现场照片…' : '暂无可用现场照片' }}</span><small>{{ selectedDeviceInfo?.latest_image_captured_ms ? `最新照片 · ${date(selectedDeviceInfo.latest_image_captured_ms)}` : '等待设备上传照片' }}</small></div>
          <div v-if="eventAt" class="event-context"><b>事件轨迹 · {{ date(eventAt) }}</b><span>事发前后各 5 分钟</span><button v-if="eventId" type="button" @click="openEventCenter(eventId)">查看事件证据 ↗</button></div>
          <div v-if="trackError" class="track-notice" role="status">轨迹更新失败，保留上次结果。<button @click="loadSelectedTrack(selectedDevice)">重试</button></div>
          <div v-if="trackSegments.length > 1" class="track-notice">轨迹分为 {{ trackSegments.length }} 段，定位中断或异常跳点处未连线。</div>
          <div class="track-tools"><div class="track-ranges" role="group" aria-label="轨迹时间范围"><button type="button" :class="{ active: !eventAt && trackWindowMs === 900000 }" @click="setTrackWindow(900000)">近 15 分钟</button><button type="button" :class="{ active: !eventAt && trackWindowMs === 3600000 }" @click="setTrackWindow(3600000)">近 1 小时</button></div><span>{{ trackLoading ? '轨迹加载中…' : validTracks.length ? `${time(validTracks[0].captured_ms)} — ${time(validTracks[validTracks.length - 1].captured_ms)}` : '暂无有效 GPS 轨迹' }}</span></div>
          <div class="track-playback"><button type="button" :disabled="validTracks.length < 2" @click="playing ? pausePlayback() : startPlayback()">{{ playing ? '暂停' : playbackTotal && playbackIndex === playbackTotal - 1 ? '重新播放' : playbackTotal ? '继续回放' : '播放轨迹' }}</button><input class="playback-seek" type="range" aria-label="回放进度" :min="0" :max="Math.max(1, playbackTotal - 1)" :value="playbackIndex" :disabled="!playbackTotal" @input="seekPlayback(Number(($event.target as HTMLInputElement).value))" /><select v-model.number="playbackSpeed" aria-label="回放倍速"><option :value="1">1×</option><option :value="10">10×</option><option :value="30">30×</option><option :value="60">60×</option></select></div>
          <span v-if="playbackTotal" class="playback-time">回放时间 {{ time(playbackTime) }} · {{ playbackIndex + 1 }} / {{ playbackTotal }}</span>
          <div v-if="validTracks.length > 1" class="speed-history"><div><b>车速变化</b><small>所选时段 · km/h</small></div><svg viewBox="0 0 300 54" preserveAspectRatio="none" role="img" aria-label="轨迹车速变化"><polyline :points="speedLine" fill="none" stroke="#e96532" stroke-width="2.5" vector-effect="non-scaling-stroke" stroke-linecap="round" stroke-linejoin="round" /></svg></div>
        </div>
        <div v-show="!mobileMap || !selectedDevice || sheetView === 'fleet'" class="fleet-browser">
        <el-input v-model="deviceSearch" class="fleet-search" clearable placeholder="搜索骑手或设备" aria-label="搜索骑手或设备" />
        <div class="list-head"><span>当前车队 <b>{{ devices.length }}</b></span><span>车速 / 坐标</span></div>
        <div class="device-list">
          <button v-for="device in filteredDevices" :key="device.device_id" class="device-row" :class="{ selected: selectedDevice === device.device_id }" @click="selectDevice(device.device_id)">
            <span class="dot" :class="online(device) && telemetryFresh(device) ? device.crowd_mode ? 'crowd' : 'online' : 'offline'"></span>
            <span class="device-name"><b>{{ device.rider_name || device.device_id }}</b><small>{{ device.device_id }}</small></span>
            <span class="device-data">{{ !telemetryFresh(device) || device.last_speed == null ? '—' : `${Number(device.last_speed).toFixed(1)} km/h` }}<small>{{ device.gps_valid && device.last_lat != null ? `${Number(device.last_lat).toFixed(5)}, ${Number(device.last_lng).toFixed(5)}` : '定位未就绪' }}</small></span>
            <span class="device-state">{{ !online(device) ? '离线' : !device.last_sample_ms || Date.now() - device.last_sample_ms > 15000 ? '数据过期' : !device.gps_valid ? '定位无效' : device.inference_status === 'ERROR' ? '识别失败' : device.crowd_mode ? '人群密集' : '正常' }}</span>
          </button>
          <p v-if="devices.length && !filteredDevices.length" class="empty">未找到匹配的骑手或设备</p>
          <p v-if="!devices.length" class="empty">暂无设备。前往“骑手与设备”页面创建设备后即可查看位置。</p>
        </div>
        </div>
        <footer v-if="selectedDevice"><span class="footer-mark"></span>点击地图标记或列表切换骑手 <span class="footer-separator">/</span> 绿色起点，橙色终点</footer>
        </div>
        </div>
        </div>
      </section>

    </div>

    <div v-if="page === 'fleet' || page === 'policy'" class="bottom-grid">
      <section v-if="page === 'fleet'" class="panel">
        <div class="panel-head"><div><span class="eyebrow">FLEET SETUP</span><h2>骑手与设备</h2></div><button v-if="canManage" class="text-button" @click="createRider">+ 新建骑手</button></div>
        <div v-if="canManage" class="create-row"><el-input v-model="newDeviceId" placeholder="设备编号，如 SG-001" /><el-select v-model="newDeviceRider" placeholder="绑定骑手" clearable><el-option v-for="rider in riders" :key="rider.id" :label="rider.name" :value="rider.id" /></el-select><el-button type="primary" @click="createDevice">创建设备</el-button></div>
        <p v-if="canManage" class="help">设备凭证仅展示一次，请复制到模拟器或 STM32 配置。</p>
        <div class="binding"><div v-for="device in devices" :key="device.device_id"><b>{{ device.device_id }}</b><el-select :model-value="device.rider_id || undefined" placeholder="未绑定" clearable :disabled="!canManage" @change="(value: number | undefined) => changeBinding(device.device_id, value)"><el-option v-for="rider in riders" :key="rider.id" :label="rider.name" :value="rider.id" /></el-select><button v-if="canManage" class="text-button" @click="rotateToken(device.device_id)">重置凭证</button></div></div>
      </section>
      <section v-if="page === 'policy'" class="panel">
        <div class="panel-head"><div><span class="eyebrow">SPEED POLICY</span><h2>分场景限速</h2></div><span class="tag">演示配置</span></div>
        <div class="policy-grid"><label>普通限速 <el-input-number v-model="policy.normal_limit_kph" :disabled="!canManage" :min="2" :max="80" /> km/h</label><label>人群密集限速 <el-input-number v-model="policy.crowd_limit_kph" :disabled="!canManage" :min="1" :max="79" /> km/h</label><label>密集人数阈值 <el-input-number v-model="policy.crowd_person_count" :disabled="!canManage" :min="1" :max="50" /> 人</label></div>
        <p class="help">单张图片达到人数阈值后立即切换；连续三张低于阈值后恢复。数值仅供联调。</p>
        <el-button v-if="canManage" type="primary" @click="updatePolicy">保存限速规则</el-button>
      </section>
      <OperationsPanel :devices="devices" />
    </div>
    <el-dialog v-model="imageDialog" title="风险现场照片" width="min(720px, 92vw)" @closed="clearImage"><img v-if="evidenceUrl" :src="evidenceUrl" class="evidence" alt="风险事件现场照片" /></el-dialog>
  </main>
</template>

<script setup lang="ts" name="Index">
import { ElMessage, ElMessageBox } from 'element-plus';
import { addDevice, addRider, bindDevice, getDemoIntersection, getDevices, getEvidenceImage, getOverview, getPolicy, getRiders, getTracks, rotateDeviceToken, savePolicy } from '@/api/riderguard';
import type { DemoIntersection, Device, Overview, Policy, Rider, Track } from '@/api/riderguard';
import { useUserStore } from '@/store/modules/user';
import OperationsPanel from '@/views/riderguard/OperationsPanel.vue';
import { distanceMeters, splitTracks, validFix } from '@/utils/riderguard-track';

const { width: viewportWidth } = useWindowSize();
const mobileMap = computed(() => viewportWidth.value <= 992 && page.value === 'map');
const sheetView = ref<'detail' | 'fleet'>('fleet');
const mobileSheetOpen = ref(false), sheetContent = ref<HTMLElement>();
let sheetStartY = 0, sheetDragged = false;
function startSheetGesture(event: PointerEvent) {
  sheetStartY = event.clientY; sheetDragged = false;
  (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
}
function endSheetGesture(event: PointerEvent) {
  const delta = event.clientY - sheetStartY;
  sheetDragged = Math.abs(delta) > 24;
  if (sheetDragged) mobileSheetOpen.value = delta < 0;
}
watch(sheetView, () => { void nextTick(() => sheetContent.value?.scrollTo({ top: 0 })); });
function toggleSheet(event: MouseEvent) {
  if (!sheetDragged || event.detail === 0) mobileSheetOpen.value = !mobileSheetOpen.value;
  sheetDragged = false;
}
const mapPanel = ref<HTMLElement>();
const railCollapsed = ref(false), mapFullscreen = ref(false), followRider = ref(false);
const deviceSearch = ref(''), riderPhotoUrl = ref(''), riderPhotoLoading = ref(false);
let photoVersion = 0;
let mapResizeObserver: ResizeObserver | undefined;
const currentRoute = useRoute();
const router = useRouter();
const userStore = useUserStore();
const page = computed(() => currentRoute.path.endsWith('/live') ? 'live' : currentRoute.path.endsWith('/map') ? 'map' : currentRoute.path.endsWith('/fleet') ? 'fleet' : currentRoute.path.endsWith('/policy') ? 'policy' : 'overview');
const isMapView = computed(() => page.value === 'overview' || page.value === 'live' || page.value === 'map');
const pageHeading = computed(() => ({ overview: '骑手安全运营中心', live: '实时骑行态势', map: '骑手地图与轨迹', events: '风险处置中心', fleet: '骑手与设备', policy: '规则与运维' })[page.value]);
const pageDescription = computed(() => ({ overview: '把实时路况、骑手位置与风险处置汇集到同一视野。', live: '跟踪骑手位置、车速与近一小时轨迹。', map: '在大地图上查看实时位置，点选骑手复盘轨迹与车速。', events: '核实风险证据，记录完整处置过程。', fleet: '管理骑手绑定关系，检查设备链路状态。', policy: '调整联调规则，观察云端识别与风险趋势。' })[page.value]);
const canManage = computed(() => userStore.roles.includes('superadmin') || userStore.roles.includes('riderguard_manager'));

const overview = reactive<Overview>({ onlineDevices: 0, riders: 0, todayEvents: 0, openEvents: 0 });
const devices = ref<Device[]>([]), riders = ref<Rider[]>([]), tracks = ref<Track[]>([]);
const policy = reactive<Policy>({ normal_limit_kph: 25, crowd_limit_kph: 10, crowd_person_count: 3 });
const newDeviceId = ref(''), newDeviceRider = ref<number>(), selectedDevice = ref(''), refreshedAt = ref(0);
const imageDialog = ref(false), evidenceUrl = ref(''), mapElement = ref<HTMLElement>();
const demoIntersection = ref<DemoIntersection>(), signalDetailsOpen = ref(false), signalClock = ref(Date.now());
const displaySignalState = computed(() => demoIntersection.value?.signal && signalClock.value < demoIntersection.value.signal.validUntilMs ? demoIntersection.value.signal.state : 'UNKNOWN');
const signalRemaining = computed(() => Math.max(0, Math.ceil(((demoIntersection.value?.signal?.remainingSeconds || 0) * 1000 - (signalClock.value - (demoIntersection.value?.signal?.observedAtMs || 0))) / 1000)));
const signalLabel = computed(() => ({ RED: '红灯', YELLOW: '黄灯', GREEN: '绿灯', UNKNOWN: '状态未知' })[displaySignalState.value]);
const refreshing = ref(false), mapError = ref(false), trackLoading = ref(false), backendError = ref(false);
const trackWindowMs = ref(3600000);
const eventAt = ref<number>();
const eventId = ref<number>();
const trackError = ref(false);
const playbackSpeed = ref(10);
const playbackTime = ref(0);
const trackSegments = computed(() => splitTracks(tracks.value));
const playing = ref(false), playbackIndex = ref(0), playbackTotal = ref(0);
const mapKey = import.meta.env.VITE_AMAP_KEY || '';
const locatedCount = computed(() => devices.value.filter(d => online(d) && telemetryFresh(d) && d.gps_valid && d.last_lat != null && d.last_lng != null).length);

const filteredDevices = computed(() => devices.value.filter(device => `${device.rider_name || ''} ${device.device_id}`.toLowerCase().includes(deviceSearch.value.trim().toLowerCase())));
const selectedDeviceInfo = computed(() => devices.value.find(device => device.device_id === selectedDevice.value));
const validTracks = computed(() => tracks.value.filter(validFix));
const speedLine = computed(() => {
  const points = validTracks.value;
  if (points.length < 2) return '';
  const sampled = points.filter((_, index) => index % Math.max(1, Math.ceil(points.length / 80)) === 0);
  if (sampled[sampled.length - 1] !== points[points.length - 1]) sampled.push(points[points.length - 1]);
  const max = Math.max(30, ...sampled.map(point => point.speed_kph));
  return sampled.map((point, index) => `${index / (sampled.length - 1) * 300},${50 - point.speed_kph / max * 44}`).join(' ');
});
const metricItems = computed(() => [
  { index: '01 / CONNECTED', label: '在线设备', value: overview.onlineDevices, unit: '台', note: '15 秒内有数据', tone: 'connected' },
  { index: '02 / RIDERS', label: '骑手档案', value: overview.riders, unit: '人', note: '已录入骑手', tone: 'riders' },
  { index: '03 / 24H EVENTS', label: '近 24 小时事件', value: overview.todayEvents, unit: '起', note: '含模拟路口提醒', tone: 'events' },
  { index: '04 / ACTION NEEDED', label: '待处置事件', value: overview.openEvents, unit: '起', note: '需要人工核实', tone: 'attention' }
]);
let timer: ReturnType<typeof setInterval> | undefined, amap: any, map: any, route: any, routeStart: any, routeEnd: any, signalMarker: any;
let playbackTimer: ReturnType<typeof setInterval> | undefined, playbackMarker: any, playbackVersion = 0;
let playbackPoints: Track[] = [], playbackCoordinates: any[] = [];
let eventMarker: any;
let routeSelectionVersion = 0;
const markerAnimations = new Map<string, number>();
const markerTargets = new Map<string, string>();
let mapDrawVersion = 0, trackRequestVersion = 0, routeSignature = '', fitRequested = true, lastFullTrackAt = 0, trackPending = false;
let lastSummaryAt = 0, lastSecondaryPollAt = 0;
const markers = new Map<string, any>();
const markerStates = new Map<string, string>();
const convertedCoordinates = new Map<string, any>();
const time = (ms: number) => new Date(ms).toLocaleTimeString('zh-CN', { hour12: false });
const date = (ms: number) => new Date(ms).toLocaleString('zh-CN', { hour12: false });
const online = (d: Device) => Boolean(d.last_seen_ms && Date.now() - d.last_seen_ms < 15000);
const telemetryFresh = (d: Device) => Boolean(d.last_sample_ms && Date.now() - d.last_sample_ms < 15000);

function clearRiderPhoto() {
  if (riderPhotoUrl.value) URL.revokeObjectURL(riderPhotoUrl.value);
  riderPhotoUrl.value = '';
}
watch([() => selectedDevice.value, () => selectedDeviceInfo.value?.latest_image_id], async () => {
  const version = ++photoVersion;
  clearRiderPhoto();
  const id = selectedDeviceInfo.value?.latest_image_id;
  riderPhotoLoading.value = Boolean(id);
  if (!id) return;
  try {
    const blob = await getEvidenceImage(id);
    if (version === photoVersion) riderPhotoUrl.value = URL.createObjectURL(blob);
  } catch { /* The photo slot retains an explicit unavailable state. */ }
  finally { if (version === photoVersion) riderPhotoLoading.value = false; }
});
async function toggleMapFullscreen() {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await mapPanel.value?.requestFullscreen();
  } catch { ElMessage.info('当前浏览器暂不支持全屏，请使用收起面板扩大地图'); }
}
function syncFullscreen() { mapFullscreen.value = document.fullscreenElement === mapPanel.value; }
function toggleFollow() { followRider.value = !followRider.value; if (followRider.value) void drawMap(); }

async function refresh(force = false) {
  if (refreshing.value) return;
  refreshing.value = true;
  try {
    if (isMapView.value) {
      const [list, intersection] = await Promise.all([getDevices(), getDemoIntersection().catch(() => undefined)]);
      devices.value = list.data || [];
      demoIntersection.value = intersection?.data;
      if (force || Date.now() - lastSummaryAt >= 10000) {
        const summary = await getOverview();
        Object.assign(overview, summary.data || {});
        lastSummaryAt = Date.now();
      }
      await drawMap();
    } else if (page.value === 'fleet') {
      const [list, people] = await Promise.all([getDevices(), getRiders()]);
      devices.value = list.data || []; riders.value = people.data || [];
    } else if (page.value === 'policy') {
      const [list, rules] = await Promise.all([getDevices(), getPolicy()]);
      devices.value = list.data || []; Object.assign(policy, rules.data || {});
    } else {
      const summary = await getOverview();
      Object.assign(overview, summary.data || {});
    }
    refreshedAt.value = Date.now(); backendError.value = false;
  } catch { backendError.value = true; /* request.ts shows the backend error */ }
  finally { refreshing.value = false; }
}
async function loadSelectedTrack(id: string, incremental = false) {
  if (incremental && trackPending) return;
  const requestVersion = ++trackRequestVersion;
  const now = eventAt.value ? eventAt.value + 300000 : Date.now();
  const windowStart = eventAt.value ? eventAt.value - 300000 : now - trackWindowMs.value;
  const fromMs = incremental && tracks.value.length ? Math.max(windowStart, tracks.value[tracks.value.length - 1].captured_ms + 1) : windowStart;
  trackPending = true;
  if (!incremental) trackLoading.value = true;
  try {
    const result = fromMs <= now ? await getTracks(id, fromMs, now) : { data: [] as Track[] };
    if (requestVersion !== trackRequestVersion || selectedDevice.value !== id) return;
    trackError.value = false;
    tracks.value = incremental ? [...tracks.value.filter(track => track.captured_ms >= windowStart), ...(result.data || [])] : result.data || [];
    if (!incremental) lastFullTrackAt = now;
    await drawMap();
  } catch { if (requestVersion === trackRequestVersion) trackError.value = true; }
  finally { if (requestVersion === trackRequestVersion) { trackPending = false; trackLoading.value = false; } }
}
async function selectDevice(id: string, historical = false) {
  railCollapsed.value = false; followRider.value = false;
  mobileSheetOpen.value = true; sheetView.value = 'detail';
  void nextTick(() => sheetContent.value?.scrollTo({ top: 0 }));
  if (!historical) routeSelectionVersion++;
  if (!historical && eventAt.value) {
    eventAt.value = undefined; eventId.value = undefined;
    if (eventMarker && map) map.remove(eventMarker);
    eventMarker = undefined;
    selectedDevice.value = '';
  }
  if (selectedDevice.value === id) { fitRequested = true; await drawMap(); return; }
  stopPlayback();
  trackRequestVersion++;
  selectedDevice.value = id;
  tracks.value = [];
  fitRequested = true;
  await drawMap();
  if (selectedDevice.value === id) await loadSelectedTrack(id);
}
async function setTrackWindow(windowMs: number) {
  routeSelectionVersion++;
  if (trackWindowMs.value === windowMs && !eventAt.value) return;
  eventAt.value = undefined; eventId.value = undefined;
  if (eventMarker && map) map.remove(eventMarker);
  eventMarker = undefined;
  stopPlayback();
  trackRequestVersion++;
  trackWindowMs.value = windowMs;
  tracks.value = [];
  fitRequested = true;
  await drawMap();
  if (selectedDevice.value) await loadSelectedTrack(selectedDevice.value);
}
async function clearSelectedDevice() {
  sheetView.value = 'fleet';
  followRider.value = false;
  routeSelectionVersion++;
  stopPlayback();
  trackRequestVersion++;
  trackPending = false;
  trackLoading.value = false;
  selectedDevice.value = '';
  eventAt.value = undefined; eventId.value = undefined;
  if (eventMarker && map) map.remove(eventMarker);
  eventMarker = undefined;
  tracks.value = [];
  fitRequested = true;
  await drawMap();
}
function pausePlayback() {
  if (playbackTimer) clearInterval(playbackTimer);
  playbackTimer = undefined;
  playing.value = false;
}
function stopPlayback() {
  playbackVersion++;
  pausePlayback();
  if (playbackMarker && map) map.remove(playbackMarker);
  playbackMarker = undefined;
  playbackPoints = []; playbackCoordinates = [];
  playbackIndex.value = 0; playbackTotal.value = 0; playbackTime.value = 0;
}
function seekPlayback(value: number) {
  playbackIndex.value = value;
  playbackTime.value = playbackPoints[value]?.captured_ms || 0;
  playbackMarker?.setPosition(playbackCoordinates[value]);
}
async function startPlayback() {
  if (!map || !amap || validTracks.value.length < 2) return;
  if (!playbackMarker) {
    stopPlayback();
    const version = playbackVersion;
    const points = [...validTracks.value].sort((a, b) => a.captured_ms - b.captured_ms || a.id - b.id);
    const coordinates = await convert(points.map(point => [Number(point.lng), Number(point.lat)]));
    if (version !== playbackVersion || !map) return;
    playbackPoints = points; playbackCoordinates = coordinates;
    playbackTotal.value = points.length;
    playbackMarker = new amap.Marker({ position: coordinates[0], content: '<span class="rg-playback-pin"></span>', offset: new amap.Pixel(-11, -11), zIndex: 120 });
    map.add(playbackMarker);
    seekPlayback(0);
  }
  if (playbackIndex.value >= playbackTotal.value - 1) seekPlayback(0);
  pausePlayback();
  playing.value = true;
  let previousTick = performance.now();
  playbackTimer = setInterval(() => {
    const now = performance.now();
    playbackTime.value += (now - previousTick) * playbackSpeed.value;
    previousTick = now;
    let index = playbackIndex.value;
    while (index < playbackPoints.length - 1 && playbackPoints[index + 1].captured_ms <= playbackTime.value) index++;
    if (index !== playbackIndex.value) {
      playbackIndex.value = index;
      playbackMarker?.setPosition(playbackCoordinates[index]);
    }
    if (index === playbackPoints.length - 1) pausePlayback();
  }, 100);
}
async function createRider() {
  try {
    const { value: name } = await ElMessageBox.prompt('输入骑手姓名', '新建骑手', { inputPattern: /\S+/, inputErrorMessage: '请输入姓名' });
    const { value: phone } = await ElMessageBox.prompt('输入联系电话（可留空）', '骑手联系方式', { inputValue: '' });
    await addRider(name, phone); riders.value = (await getRiders()).data || []; await refresh();
  } catch { /* user cancelled */ }
}
async function showToken(id: string, token: string) {
  await ElMessageBox.alert(`设备 ${id} 的凭证（仅展示一次）：\n${token}\n\n运行：python device/simulator.py --device-id ${id} --token ${token}`, '保存设备凭证', { confirmButtonText: '我已保存' });
}
async function createDevice() {
  if (!newDeviceId.value.trim()) { ElMessage.warning('请输入设备编号'); return; }
  try { const result = await addDevice(newDeviceId.value.trim(), newDeviceRider.value); if (result.data) await showToken(result.data.deviceId, result.data.token); newDeviceId.value = ''; await refresh(); }
  catch { /* request.ts reports errors */ }
}
async function rotateToken(id: string) {
  try { await ElMessageBox.confirm(`重置 ${id} 的凭证后原设备会失去上传权限。`, '重置设备凭证', { type: 'warning' }); const result = await rotateDeviceToken(id); if (result.data) await showToken(id, result.data.token); }
  catch { /* user cancelled */ }
}
async function changeBinding(id: string, riderId?: number) { await bindDevice(id, riderId); await refresh(); }
async function updatePolicy() {
  if (policy.crowd_limit_kph >= policy.normal_limit_kph) { ElMessage.warning('人群密集限速必须低于普通限速'); return; }
  await savePolicy(policy.normal_limit_kph, policy.crowd_limit_kph, policy.crowd_person_count); ElMessage.success('限速规则已保存');
}
function openEventCenter(id: number) { void router.push({ path: '/riderguard/events', query: { event: String(id) } }); }
async function viewImage(id: number) { clearImage(); evidenceUrl.value = URL.createObjectURL(await getEvidenceImage(id)); imageDialog.value = true; }
function clearImage() { if (evidenceUrl.value) URL.revokeObjectURL(evidenceUrl.value); evidenceUrl.value = ''; }
async function selectRouteDevice() {
  const version = ++routeSelectionVersion;
  if (!isMapView.value || typeof currentRoute.query.device !== 'string') return;
  const captured = Number(currentRoute.query.at);
  eventAt.value = Number.isFinite(captured) && captured > 0 ? captured : undefined;
  eventId.value = eventAt.value ? Number(currentRoute.query.event) || undefined : undefined;
  stopPlayback();
  selectedDevice.value = '';
  await selectDevice(currentRoute.query.device, true);
  if (version !== routeSelectionVersion) return;
  if (eventMarker && map) map.remove(eventMarker);
  eventMarker = undefined;
  const lat = Number(currentRoute.query.lat), lng = Number(currentRoute.query.lng);
  if (map && currentRoute.query.lat != null && currentRoute.query.lng != null && Number.isFinite(lat) && Number.isFinite(lng) && Math.abs(lat) <= 90 && Math.abs(lng) <= 180) {
    const [position] = await convert([[lng, lat]]);
    if (!map || version !== routeSelectionVersion) return;
    eventMarker = new amap.Marker({ position, content: '<span class="rg-event-pin">!</span>', offset: new amap.Pixel(-15, -15), zIndex: 130, title: '事发位置 · 点击查看事件' });
    const id = eventId.value;
    eventMarker.on('click', () => { if (id) openEventCenter(id); });
    map.add(eventMarker);
    map.setZoomAndCenter(16, position);
  }
}
function animateMarker(id: string, marker: any, target: any, fresh: boolean) {
  const to = [Number(target.getLng?.() ?? target[0]), Number(target.getLat?.() ?? target[1])];
  const signature = to.join(',');
  if (markerTargets.get(id) === signature) return;
  markerTargets.set(id, signature);
  const frame = markerAnimations.get(id);
  if (frame) cancelAnimationFrame(frame);
  markerAnimations.delete(id);
  const position = marker.getPosition();
  const from = [position.getLng(), position.getLat()];
  if (!fresh || document.hidden || window.matchMedia('(prefers-reduced-motion: reduce)').matches || distanceMeters(from, to) > 80) {
    marker.setPosition(target); return;
  }
  const start = performance.now();
  const tick = (now: number) => {
    const progress = Math.min(1, (now - start) / 1200);
    marker.setPosition([from[0] + (to[0] - from[0]) * progress, from[1] + (to[1] - from[1]) * progress]);
    if (progress < 1) markerAnimations.set(id, requestAnimationFrame(tick));
    else markerAnimations.delete(id);
  };
  markerAnimations.set(id, requestAnimationFrame(tick));
}
function convert(points: number[][]): Promise<any[]> {
  if (!amap || !points.length) return Promise.resolve(points);
  const key = (point: number[]) => `${point[0]},${point[1]}`;
  const missing = [...new Map(points.filter(point => !convertedCoordinates.has(key(point))).map(point => [key(point), point])).values()];
  const chunks: number[][][] = []; for (let i = 0; i < missing.length; i += 40) chunks.push(missing.slice(i, i + 40));
  return Promise.all(chunks.map(chunk => new Promise<void>(resolve => amap.convertFrom(chunk, 'gps', (status: string, result: any) => {
    if (status === 'complete' && result?.locations?.length === chunk.length) result.locations.forEach((location: any, index: number) => convertedCoordinates.set(key(chunk[index]), location));
    resolve();
  })))).then(() => points.map(point => convertedCoordinates.get(key(point)) || point));
}
function sampleRoute(history: Track[]): Track[] {
  if (history.length <= 500) return history;
  const step = Math.ceil((history.length - 1) / 499);
  const sampled = history.filter((_, index) => index % step === 0);
  if (sampled[sampled.length - 1] !== history[history.length - 1]) sampled.push(history[history.length - 1]);
  return sampled;
}
async function loadMap() {
  if (!mapKey || !mapElement.value) return;
  const win = window as any;
  const serviceHost = import.meta.env.VITE_AMAP_SERVICE_HOST || '/_AMapService';
  // eslint-disable-next-line no-underscore-dangle
  win._AMapSecurityConfig = { serviceHost: new URL(serviceHost, window.location.origin).href.replace(/\/$/, '') };
  if (!win.AMap) {
    await new Promise<void>((resolve, reject) => { const script = document.createElement('script'); script.src = `https://webapi.amap.com/maps?v=2.0&key=${encodeURIComponent(mapKey)}`; script.onload = () => resolve(); script.onerror = () => reject(new Error('地图加载失败')); document.head.appendChild(script); });
  }
  amap = win.AMap; map = new amap.Map(mapElement.value, { zoom: 12, center: [108.9342, 34.2304], mapStyle: 'amap://styles/whitesmoke' });
  map.on('dragstart', () => { followRider.value = false; });
  mapResizeObserver?.disconnect();
  mapResizeObserver = new ResizeObserver(() => map?.resize());
  mapResizeObserver.observe(mapElement.value);
  await drawMap();
}
async function retryMap() {
  mapError.value = false;
  await nextTick();
  try { await loadMap(); } catch { mapError.value = true; ElMessage.warning('地图仍不可用，请检查高德服务'); }
}
async function drawMap() {
  if (!map || !amap) return;
  const version = ++mapDrawVersion;
  if (demoIntersection.value?.enabled && demoIntersection.value.latitude != null && demoIntersection.value.longitude != null) {
    const [position] = await convert([[demoIntersection.value.longitude, demoIntersection.value.latitude]]);
    if (version !== mapDrawVersion || !map) return;
    const content = `<span class="rg-signal-pin ${displaySignalState.value.toLowerCase()}">🚦<small>MOCK</small></span>`;
    if (signalMarker) { signalMarker.setPosition(position); signalMarker.setContent(content); }
    else {
      signalMarker = new amap.Marker({ position, content, offset: new amap.Pixel(-22, -38), zIndex: 115, title: 'DEMO-001 · 模拟红绿灯' });
      signalMarker.on('click', () => { signalDetailsOpen.value = true; });
      map.add(signalMarker);
    }
  } else if (signalMarker) { map.remove(signalMarker); signalMarker = undefined; }
  const located = devices.value.filter(d => online(d) && telemetryFresh(d) && d.gps_valid && d.last_lat != null && d.last_lng != null);
  const positions = await convert(located.map(d => [Number(d.last_lng), Number(d.last_lat)]));
  if (version !== mapDrawVersion || !map) return;
  const visibleIds = new Set(located.map(d => d.device_id));
  for (const [id, marker] of markers) if (!visibleIds.has(id)) { map.remove(marker); markers.delete(id); markerStates.delete(id); markerTargets.delete(id); const frame = markerAnimations.get(id); if (frame) cancelAnimationFrame(frame); markerAnimations.delete(id); }
  located.forEach((device, index) => {
    const id = device.device_id;
    const state = online(device) && telemetryFresh(device) ? device.crowd_mode ? 'crowd' : 'online' : 'offline';
    const visualState = `${state}${selectedDevice.value === id ? ' selected' : ''}`;
    const title = `${device.rider_name || id} · ${device.last_speed || 0} km/h`;
    const marker = markers.get(id);
    if (marker) {
      animateMarker(id, marker, positions[index], online(device) && telemetryFresh(device)); marker.setTitle(title);
      if (markerStates.get(id) !== visualState) marker.setContent(`<span class="rg-map-pin ${visualState}"></span>`);
    } else {
      const created = new amap.Marker({ position: positions[index], title, content: `<span class="rg-map-pin ${visualState}"></span>` });
      created.on('click', () => { void selectDevice(id); });
      markers.set(id, created); map.add(created);
    }
    markerStates.set(id, visualState);
  });
  const history = validTracks.value;
  const segments = trackSegments.value;
  const shown = segments.map(sampleRoute);
  const nextSignature = selectedDevice.value && history.length > 1 ? `${selectedDevice.value}:${trackWindowMs.value}:${history.length}:${history[0].id}:${history[history.length - 1].id}:${segments.map(segment => `${segment[0].id}-${segment.length}`).join(",")}` : '';
  if (nextSignature !== routeSignature) {
    const paths = nextSignature ? await Promise.all(shown.filter(segment => segment.length > 1).map(segment => convert(segment.map(t => [Number(t.lng), Number(t.lat)])))) : [];
    const path = paths.flat();
    if (version !== mapDrawVersion || !map) return;
    if (path.length > 1) {
      if (route) route.setPath(paths);
      else { route = new amap.Polyline({ path: paths, strokeColor: '#e96532', strokeWeight: 5, strokeOpacity: .88 }); map.add(route); }
      if (routeStart) routeStart.setPosition(path[0]);
      else { routeStart = new amap.Marker({ position: path[0], content: '<span class="rg-route-endpoint start">起</span>', offset: new amap.Pixel(-13, -13) }); map.add(routeStart); }
      if (routeEnd) routeEnd.setPosition(path[path.length - 1]);
      else { routeEnd = new amap.Marker({ position: path[path.length - 1], content: '<span class="rg-route-endpoint end">终</span>', offset: new amap.Pixel(-13, -13) }); map.add(routeEnd); }
    } else {
      if (route) { map.remove(route); route = undefined; }
      if (routeStart) { map.remove(routeStart); routeStart = undefined; }
      if (routeEnd) { map.remove(routeEnd); routeEnd = undefined; }
    }
    routeSignature = nextSignature;
  }
  if (followRider.value && !eventAt.value && selectedDeviceInfo.value && online(selectedDeviceInfo.value) && telemetryFresh(selectedDeviceInfo.value)) {
    const index = located.findIndex(device => device.device_id === selectedDevice.value);
    if (index >= 0) { map.setCenter(positions[index]); fitRequested = false; }
  }
  if (fitRequested) {
    if (selectedDevice.value && route) map.setFitView([route]);
    else if (markers.size) map.setFitView([...markers.values()]);
    if (route || markers.size) fitRequested = false;
  }
}
onMounted(async () => {
  try { Object.assign(policy, (await getPolicy()).data || {}); riders.value = (await getRiders()).data || []; } catch { /* shown by request */ }
  await refresh(true);
  if (isMapView.value) {
    try { await loadMap(); await selectRouteDevice(); } catch { mapError.value = true; ElMessage.warning('高德地图加载失败，仍可查看定位列表'); }
  }
  lastSecondaryPollAt = Date.now();
  timer = setInterval(() => {
    if (document.hidden) return;
    signalClock.value = Date.now();
    if (isMapView.value) {
      void refresh();
      if (selectedDevice.value && !eventAt.value) void loadSelectedTrack(selectedDevice.value, Date.now() - lastFullTrackAt < 30000);
    } else if (page.value === 'policy' && Date.now() - lastSecondaryPollAt >= 15000) {
      lastSecondaryPollAt = Date.now();
      void getDevices().then(list => { devices.value = list.data || []; backendError.value = false; }).catch(() => { backendError.value = true; });
    } else if (Date.now() - lastSecondaryPollAt >= 15000) {
      lastSecondaryPollAt = Date.now();
      void refresh();
    }
  }, 2000);
});
watch(page, async () => {
  routeSelectionVersion++;
  stopPlayback();
  await refresh(true);
  if (isMapView.value) {
    await nextTick();
    try {
      if (!map) await loadMap();
      else { map.resize(); fitRequested = true; await drawMap(); }
      await selectRouteDevice();
    } catch { mapError.value = true; }
  }
});
watch(() => [currentRoute.query.device, currentRoute.query.at, currentRoute.query.event], () => { void selectRouteDevice(); });
function onVisibilityChange() { if (document.hidden) pausePlayback(); else if (isMapView.value) void refresh(true); }
onMounted(() => { document.addEventListener('visibilitychange', onVisibilityChange); document.addEventListener('fullscreenchange', syncFullscreen); });
onUnmounted(() => { photoVersion++; clearRiderPhoto(); mapResizeObserver?.disconnect(); document.removeEventListener('fullscreenchange', syncFullscreen); routeSelectionVersion++; document.removeEventListener('visibilitychange', onVisibilityChange); markerAnimations.forEach(cancelAnimationFrame); markerAnimations.clear(); if (timer) clearInterval(timer); stopPlayback(); mapDrawVersion++; trackRequestVersion++; clearImage(); map?.destroy(); map = undefined; signalMarker = undefined; markers.clear(); });
</script>

<style scoped lang="scss">
.rg-console {
  --ink: #182127;
  --muted: #77838b;
  --line: #e5eae9;
  --orange: #e96532;
  display: grid;
  gap: 20px;
  max-width: 1840px;
  margin: 0 auto;
  padding: 12px 6px 40px;
  color: var(--ink);
}
.rg-section-nav { display: flex; align-items: center; gap: 4px; overflow-x: auto; padding: 5px; border: 1px solid #e4eae8; border-radius: 11px; background: #fff; scrollbar-width: none; }
.rg-section-nav a { flex: 0 0 auto; padding: 9px 15px; border-radius: 7px; color: #69777d; font-size: 12px; font-weight: 650; text-decoration: none; transition: background-color 150ms ease, color 150ms ease; }
.rg-section-nav a:hover { background: #f3f6f5; color: #33434a; }.rg-section-nav a.router-link-exact-active { background: #202d34; color: #fff; }
h1, h2, p { margin: 0; }
button { font: inherit; }
.rg-heading, .panel-head, .list-head, .metric-top, .metric-bottom { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
.rg-heading { min-height: 70px; padding: 2px 3px 6px; }
.heading-copy { min-width: 0; }
.eyebrow { color: #cf592b; font-size: 10px; font-weight: 800; letter-spacing: .17em; line-height: 1.4; }
.eyebrow i { display: inline-block; width: 18px; height: 1px; margin: 0 7px 3px; background: #d9ab97; }
h1 { margin: 8px 0 6px; font-size: clamp(25px, 2.2vw, 32px); font-weight: 760; letter-spacing: -.045em; line-height: 1.2; }
.rg-heading p { color: var(--muted); font-size: 13px; line-height: 1.6; }
.heading-actions { display: flex; align-items: center; gap: 15px; flex-wrap: wrap; justify-content: flex-end; }
.live { display: inline-flex; align-items: center; gap: 8px; color: #22694a; font-size: 12px; font-weight: 700; white-space: nowrap; }
.live.disconnected { color: #b96a45; }.live.disconnected .live-dot { background: #df8758; box-shadow: 0 0 0 4px #df875820; }
.live-dot, .map-live-dot { width: 7px; height: 7px; border-radius: 50%; background: #33a46e; box-shadow: 0 0 0 4px #33a46e20; }
.updated { color: #94a0a5; font-size: 11px; white-space: nowrap; }
.refresh-button { display: inline-flex; align-items: center; gap: 7px; min-height: 34px; padding: 0 12px; border: 1px solid #dce2e0; border-radius: 8px; background: #fff; color: #334049; font-size: 11px; font-weight: 700; cursor: pointer; transition: border-color 160ms ease, background-color 160ms ease, transform 160ms ease; }
.refresh-button:hover { border-color: #b9c8c3; background: #f7faf8; }
.refresh-button:active { transform: scale(.97); }
.refresh-button:disabled { opacity: .6; cursor: default; }
.refresh-button span { font-size: 20px; line-height: 1; }
.refresh-button .spinning { animation: spin 800ms linear infinite; }
.metrics { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 14px; }
.metrics article, .panel { min-width: 0; border: 1px solid var(--line); border-radius: 16px; background: #fff; box-shadow: 0 8px 28px #1a2b3210; }
.metrics article { position: relative; display: grid; gap: 20px; overflow: hidden; padding: 21px 22px 19px; transition: border-color 180ms ease, box-shadow 180ms ease, transform 180ms ease; }
.metrics article::before { position: absolute; top: 0; left: 22px; width: 42px; height: 2px; background: #a4b9b3; content: ''; }
.metrics article:hover { border-color: #cdd8d4; box-shadow: 0 12px 32px #1a2b3217; transform: translateY(-2px); }
.metrics article.connected::before { background: #2da56d; }
.metrics article.events::before, .metrics article.attention::before { background: #e96532; }
.metrics article.attention { background: linear-gradient(135deg, #fff 53%, #fff8f4 100%); }
.metric-top span { color: #627078; font-size: 12px; font-weight: 650; }
.metric-top small { color: #a3adb1; font-size: 9px; font-weight: 700; letter-spacing: .06em; }
.metrics strong { font-size: clamp(35px, 3vw, 46px); font-weight: 730; letter-spacing: -.055em; line-height: 1; font-variant-numeric: tabular-nums; }
.metrics strong em { margin-left: 7px; color: #8f9a9f; font-size: 12px; font-style: normal; font-weight: 500; letter-spacing: 0; }
.metrics .attention strong { color: #d95b2d; }
.metric-bottom { justify-content: flex-start; gap: 8px; }
.metric-bottom small { color: #919ca2; font-size: 10px; }
.metric-line { width: 14px; height: 1px; background: #bdc9c7; }
.main-grid { display: grid; grid-template-columns: minmax(0, 1fr); gap: 16px; align-items: stretch; }
.main-grid.live-only { grid-template-columns: minmax(0, 1fr); }.main-grid.live-only .map { height: clamp(420px, 48vw, 680px); }
.rg-console.map-page { max-width: none; }
.main-grid.map-only { grid-template-columns: minmax(0, 1fr); }
.position-panel > .panel-head { flex-wrap: wrap; }
.map-head-actions { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.map-expand { padding: 7px 10px; border: 1px solid #e7ddd5; border-radius: 6px; color: #c65c30; font-size: 11px; font-weight: 700; text-decoration: none; transition: background-color 150ms ease, border-color 150ms ease; }
.map-expand:hover { border-color: #e9ad8c; background: #fff5ef; }
.map-page .rail-collapsed .map-workspace-body { grid-template-columns: minmax(0, 1fr); }
.map-page .map-workspace-body { display: grid; grid-template-columns: minmax(0, 1fr) minmax(310px, 350px); gap: 16px; align-items: stretch; }
.map-page .map-stage { min-width: 0; border-radius: 13px; }
.map-page .map { height: clamp(610px, calc(100vh - 220px), 980px); }
.map-page .map-rail { display: flex; flex-direction: column; min-width: 0; max-height: clamp(610px, calc(100vh - 220px), 980px); overflow-y: auto; padding: 0 5px 0 0; scrollbar-width: thin; }
.sheet-content, .fleet-browser { min-width: 0; }
.map-page .sheet-content { display: flex; flex-direction: column; min-height: 0; }
.map-page .track-inspector { flex: 0 0 auto; margin-top: 0; }
.map-page .track-stats { grid-template-columns: repeat(2, minmax(0, 1fr)); }
.map-page .track-stats > div:last-child { grid-column: 1 / -1; }
.map-page .list-head { flex: 0 0 auto; padding-top: 8px; }
.map-page .device-list { flex: 1 1 auto; max-height: none; overflow: visible; }
.map-page .device-row { display: grid; grid-template-columns: 8px minmax(0, 1fr) auto; gap: 3px 9px; }
.map-page .device-name { min-width: 0; }
.map-page .device-data { margin-left: 0; }
.map-page .device-state { grid-column: 2 / -1; justify-self: end; }
.map-page footer { flex: 0 0 auto; }
.bottom-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 16px; }
.panel { padding: 23px; }
.panel-head { margin-bottom: 19px; }
h2 { margin-top: 5px; font-size: 20px; font-weight: 740; letter-spacing: -.025em; line-height: 1.3; }
.heading-caption { color: #9ba6aa; font-size: 13px; font-weight: 450; letter-spacing: 0; }
.tag { display: inline-flex; align-items: center; min-height: 28px; padding: 0 11px; border: 1px solid #e6ebea; border-radius: 6px; background: #f7f9f8; color: #647078; font-size: 10px; font-weight: 700; white-space: nowrap; }
.map-stage { position: relative; overflow: hidden; border: 1px solid #dce5e2; border-radius: 11px; background: #e6edeb; }
.map { height: clamp(320px, 31vw, 450px); min-height: 320px; }
.map-fallback { display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 10px; padding: 55px 20px; background: radial-gradient(circle at 50% 45%, #f7faf7 0, #e8eeeb 58%, #dde7e3 100%); text-align: center; }
.fallback-symbol { display: grid; place-items: center; width: 58px; height: 58px; margin-bottom: 5px; border: 1px solid #cddbd5; border-radius: 50%; color: #83988e; font-size: 36px; line-height: 1; }
.map-fallback strong { color: #394d48; font-size: 14px; }
.map-fallback p { max-width: 310px; color: #7b9188; font-size: 11px; line-height: 1.6; }
.map-overlay { position: absolute; z-index: 2; display: inline-flex; align-items: center; gap: 9px; min-height: 28px; padding: 0 11px; border: 1px solid #e5eae8e8; border-radius: 6px; background: #fffffff2; box-shadow: 0 5px 18px #16271b1c; color: #344640; font-size: 9px; font-weight: 760; letter-spacing: .1em; pointer-events: none; }
.map-overlay-top { top: 14px; left: 14px; }
.map-overlay-count { margin-left: 7px; padding-left: 10px; border-left: 1px solid #d8e1dc; color: #8a9991; font-weight: 550; letter-spacing: 0; }
.map-signal-card { position: absolute; z-index: 3; top: 52px; left: 14px; display: flex; align-items: flex-start; gap: 9px; max-width: min(290px, calc(100% - 28px)); min-height: 46px; padding: 9px 12px; border: 1px solid #e5eae8; border-radius: 9px; background: #fffffff5; box-shadow: 0 6px 20px #182b281c; color: #253841; text-align: left; cursor: pointer; }
.map-signal-card > span:last-child { display: grid; gap: 3px; }.map-signal-card b { font-size: 11px; }.map-signal-card small { color: #728287; font-size: 10px; line-height: 1.45; }
.signal-dot { flex: 0 0 10px; width: 10px; height: 10px; margin-top: 2px; border-radius: 50%; background: #a9b6b6; }.signal-dot.red { background: #e45c43; }.signal-dot.green { background: #2caa78; }.signal-dot.yellow { background: #e6aa31; }
.map-stage :deep(.rg-signal-pin) { display: grid; justify-items: center; gap: 1px; min-width: 43px; padding: 4px 5px; border: 1px solid #dce8e3; border-radius: 9px; background: #fff; box-shadow: 0 4px 14px #1e343830; font-size: 22px; line-height: 1; }
.map-stage :deep(.rg-signal-pin small) { color: #a55b39; font-size: 8px; font-weight: 800; letter-spacing: .05em; }
.map-live-dot { width: 6px; height: 6px; }
.map-legend { right: 14px; bottom: 14px; gap: 13px; letter-spacing: 0; font-weight: 600; }
.map-legend span { display: inline-flex; align-items: center; gap: 5px; white-space: nowrap; }
.legend-dot { width: 7px; height: 7px; border-radius: 50%; }
.legend-dot.online, .dot.online { background: #29a46b; }
.legend-dot.crowd, .dot.crowd { background: #ef7a37; }
.legend-dot.offline, .dot.offline { background: #9daab0; }
.track-inspector { display: grid; gap: 14px; margin-top: 13px; padding: 16px; border: 1px solid #e8ece9; border-radius: 10px; background: linear-gradient(115deg, #f8faf8, #fff); }
.track-heading { display: flex; align-items: flex-start; justify-content: space-between; gap: 12px; }
.track-heading h3 { display: inline-block; margin: 4px 8px 0 0; color: #25343a; font-size: 16px; letter-spacing: -.02em; }
.track-id { color: #929fa4; font-size: 10px; }
.track-close { width: 26px; height: 26px; border: 1px solid #e1e7e5; border-radius: 6px; background: #fff; color: #6f7e84; font-size: 18px; line-height: 1; cursor: pointer; }
.track-close:hover { border-color: #e7b9a8; color: #ce5f36; }
.track-stats { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; }
.track-stats > div { display: grid; gap: 5px; min-width: 0; padding: 10px 12px; border: 1px solid #e8eeea; border-radius: 7px; background: #fff; }
.track-stats small { color: #87969a; font-size: 10px; }
.track-stats strong { color: #2c3d42; font-size: 15px; font-weight: 720; font-variant-numeric: tabular-nums; }
.track-stats em { color: #93a0a5; font-size: 10px; font-style: normal; font-weight: 500; }
.track-tools { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 8px; color: #87969a; font-size: 10px; }
.track-ranges { display: inline-flex; gap: 3px; padding: 3px; border: 1px solid #e4eae6; border-radius: 7px; background: #f0f4f1; }
.track-ranges button { min-height: 26px; padding: 0 9px; border: 0; border-radius: 5px; background: transparent; color: #77888d; font-size: 10px; font-weight: 650; cursor: pointer; transition: background-color 150ms ease, color 150ms ease, box-shadow 150ms ease; }
.track-ranges button.active { background: #fff; box-shadow: 0 1px 4px #142b2417; color: #c4552b; }
.track-ranges button:hover:not(.active) { color: #344b50; }
.event-context { display: grid; gap: 7px; padding: 12px; border-radius: 9px; background: #fff3eb; color: #a64b26; font-size: 12px; }
.event-context button, .track-notice button { justify-self: start; padding: 0; border: 0; background: transparent; color: #b34f25; cursor: pointer; }
.track-notice, .playback-time { color: #879497; font-size: 11px; line-height: 1.6; }
.playback-seek { flex: 1; min-width: 30px; accent-color: #e96532; }
.track-playback select { padding: 4px; border: 1px solid #d8e2dd; border-radius: 6px; background: #fff; color: #344a43; }
:deep(.rg-event-pin) { display: grid; place-items: center; width: 30px; height: 30px; border: 3px solid #fff; border-radius: 50%; background: #d94d38; color: white; font-weight: 800; box-shadow: 0 3px 12px #69251544; }
.rider-status-line { display: flex; justify-content: space-between; align-items: center; margin: 12px 0; font-size: 12px; }
.status-online { color: #287d57; }.status-offline { color: #6c7981; }
.rider-limit { display: flex; gap: 8px; align-items: center; color: #66747b; font-size: 12px; }.rider-limit b { color: #26343b; }.rider-limit span { margin-left: auto; }
.rider-photo { display: grid; gap: 7px; margin: 14px 0; padding: 8px; border-radius: 9px; background: #f1f4f3; color: #6c7981; font-size: 12px; }
.rider-photo button { padding: 0; border: 0; background: transparent; cursor: zoom-in; }.rider-photo img { display: block; width: 100%; height: 135px; object-fit: contain; border-radius: 6px; background: #e7ecea; }.rider-photo small { font-size: 11px; }
.fleet-search { margin: 8px 0; }
.position-panel:fullscreen { padding: 20px; overflow: auto; background: #f7f9f8; }
.position-panel:fullscreen .map { height: calc(100vh - 100px); }
.position-panel:fullscreen .map-rail { max-height: calc(100vh - 100px); }
.track-playback { display: flex; align-items: center; gap: 10px; min-width: 0; }
.track-playback button { flex: 0 0 auto; min-height: 29px; padding: 0 12px; border: 1px solid #d8e2dd; border-radius: 6px; background: #fff; color: #344a43; font-size: 10px; font-weight: 700; cursor: pointer; transition: border-color 150ms ease, background-color 150ms ease; }
.track-playback button:hover:not(:disabled) { border-color: #86a998; background: #f0f6f2; }
.track-playback button:disabled { opacity: .45; cursor: default; }
.track-playback > span { flex: 0 0 auto; min-width: 45px; color: #8b999b; font-size: 10px; text-align: right; font-variant-numeric: tabular-nums; }
.playback-progress { flex: 1 1 auto; height: 4px; min-width: 30px; overflow: hidden; border-radius: 5px; background: #e5ebe7; }
.playback-progress i { display: block; height: 100%; border-radius: inherit; background: #e96532; transition: width 400ms linear; }
.speed-history { display: grid; gap: 7px; padding: 10px 12px 5px; border: 1px solid #e8eeea; border-radius: 7px; background: #fff; }
.speed-history > div { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
.speed-history b { color: #34464a; font-size: 10px; }
.speed-history small { color: #93a1a2; font-size: 9px; }
.speed-history svg { width: 100%; height: 54px; overflow: visible; }
:deep(.rg-playback-pin) { display: block; width: 20px; height: 20px; border: 4px solid #fff; border-radius: 50%; background: #e96532; box-shadow: 0 0 0 2px #e96532, 0 5px 14px #5a271688; }
.list-head { padding: 20px 6px 10px; color: #87949a; font-size: 11px; font-weight: 600; }
.list-head b { margin-left: 5px; color: #b5bfc1; font-size: 10px; }
.device-list { max-height: 296px; overflow: auto; scrollbar-width: thin; }
.device-row { display: flex; align-items: center; width: 100%; min-height: 62px; gap: 12px; padding: 10px 12px; border: 1px solid transparent; border-top-color: #edf0ef; background: transparent; text-align: left; cursor: pointer; transition: border-color 150ms ease, background-color 150ms ease, transform 150ms ease; }
.device-row:hover { background: #f7faf8; }
.device-row.selected { border-color: #f4d2c3; border-radius: 7px; background: #fff7f3; }
.device-row:active { transform: scale(.997); }
.dot { flex: 0 0 8px; width: 8px; height: 8px; border-radius: 50%; }
.device-name, .device-data { display: grid; gap: 3px; min-width: 0; }
.device-name { min-width: 110px; }
.device-name b { overflow: hidden; color: #27343b; font-size: 12px; text-overflow: ellipsis; white-space: nowrap; }
.device-name small, .device-data small { overflow: hidden; color: #9aa5a8; font-size: 10px; text-overflow: ellipsis; white-space: nowrap; }
.device-data { margin-left: auto; font-size: 12px; font-weight: 700; text-align: right; font-variant-numeric: tabular-nums; }
.device-data small { font-weight: 450; }
.device-state { min-width: 62px; color: #64727a; font-size: 10px; font-weight: 650; text-align: right; }
footer { display: flex; align-items: center; gap: 8px; min-height: 32px; margin-top: 6px; padding: 10px 5px 0; border-top: 1px solid #edf0ef; color: #839198; font-size: 10px; }
.footer-mark { width: 5px; height: 5px; border-radius: 50%; background: var(--orange); }
.footer-separator { color: #c2cbce; }
.risk-panel { display: flex; flex-direction: column; border-color: #172834; background: linear-gradient(153deg, #1c2b34 0%, #121d26 54%, #0f1921 100%); box-shadow: 0 12px 32px #0d1b2a24; color: #f5f8f7; }
.risk-panel .eyebrow { color: #ff9b69; }
.risk-panel h2 { color: #f5f8f7; }
.risk-panel .text-button { color: #ffb68a; text-decoration: none; white-space: nowrap; }.risk-panel .text-button:hover { color: #ffcfac; }
.risk-panel .tag.danger { border-color: #bd663d58; background: #e9713526; color: #ffb78e; }
.risk-intro { margin: -6px 0 16px; color: #98a9ae; font-size: 11px; line-height: 1.65; }
.event-list { display: grid; align-content: start; gap: 10px; max-height: 706px; overflow: auto; padding-right: 3px; scrollbar-color: #5a6970 transparent; scrollbar-width: thin; }
.event { padding: 16px; border: 1px solid #ffffff19; border-radius: 10px; background: #ffffff0c; transition: border-color 170ms ease, background-color 170ms ease, transform 170ms ease; }
.event:hover { border-color: #ffffff36; background: #ffffff13; transform: translateY(-1px); }
.event.resolved { opacity: .7; }
.event-title { display: flex; align-items: center; gap: 9px; font-size: 12px; }
.event-title b { font-weight: 700; }
.event-title small { margin-left: auto; color: #ffb18c; font-size: 10px; white-space: nowrap; }
.event.resolved .event-title small { color: #9ec9b5; }
.warning { display: grid; flex: 0 0 24px; place-items: center; height: 24px; border: 1px solid #eb875254; border-radius: 6px; background: #e9713527; color: #ff9a65; font-weight: 800; }
.event p { margin: 11px 0 7px; color: #9eb0b5; font-size: 10px; line-height: 1.5; }
.event strong { font-size: 25px; font-weight: 690; letter-spacing: -.035em; font-variant-numeric: tabular-nums; }
.event strong small { color: #b1bfc1; font-size: 10px; font-weight: 400; letter-spacing: 0; }
.event strong small span { margin: 0 4px; color: #657b80; }
.actions { display: flex; flex-wrap: wrap; gap: 7px; margin-top: 14px; }
.actions button { min-height: 28px; padding: 0 9px; border: 1px solid #ffffff32; border-radius: 5px; background: #ffffff0b; color: #dce8e6; font-size: 10px; font-weight: 650; cursor: pointer; transition: background-color 150ms ease, border-color 150ms ease, transform 150ms ease; }
.actions button:hover { border-color: #ff9a6e9c; background: #f17e4933; }
.actions button:active { transform: scale(.97); }
.actions button:last-child { border-color: #e9653278; color: #ffc39f; }
.text-button { padding: 3px 0; border: 0; background: none; color: #d85f30; font-size: 11px; font-weight: 700; cursor: pointer; }
.text-button:hover { color: #a6401f; text-decoration: underline; text-underline-offset: 3px; }
.empty { padding: 30px 8px; color: #96a5aa; font-size: 12px; line-height: 1.7; }
.risk-panel .empty { color: #aebec1; }
.risk-empty { display: grid; justify-items: center; gap: 10px; min-height: 230px; align-content: center; padding: 24px; border: 1px dashed #ffffff2d; border-radius: 10px; background: #ffffff07; text-align: center; }
.risk-empty-icon { display: grid; place-items: center; width: 44px; height: 44px; margin-bottom: 2px; border: 1px solid #6fa88d55; border-radius: 50%; background: #66b18b1f; color: #9cd1b3; font-size: 20px; }
.risk-empty strong { color: #e5efeb; font-size: 13px; }
.risk-empty p { max-width: 240px; color: #9aafb0; font-size: 11px; line-height: 1.7; }
.create-row { display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr) auto; gap: 9px; }
.help { margin: 13px 0 15px; color: #89959b; font-size: 11px; line-height: 1.65; }
.binding { display: grid; gap: 0; max-height: 210px; overflow: auto; scrollbar-width: thin; }
.binding > div { display: grid; grid-template-columns: 1fr 1fr auto; align-items: center; gap: 10px; min-height: 53px; border-top: 1px solid #eef1f0; font-size: 11px; }
.binding b { color: #46545b; font-weight: 650; }
.policy-grid { display: flex; flex-wrap: wrap; gap: 20px; padding: 6px 0; }
.policy-grid label { display: grid; gap: 9px; color: #68767d; font-size: 11px; font-weight: 650; }
.evidence { display: block; max-width: 100%; max-height: 70vh; margin: auto; border-radius: 8px; }
:deep(.rg-map-pin) { display: block; width: 18px; height: 18px; border: 3px solid #fff; border-radius: 50%; box-shadow: 0 2px 9px #1118, 0 0 0 2px #27a46b42; background: #29a46b; }
:deep(.rg-map-pin.crowd) { background: #f17736; box-shadow: 0 2px 9px #1118, 0 0 0 2px #f1773645; }
:deep(.rg-map-pin.offline) { background: #9daab0; box-shadow: 0 2px 9px #1118; }
:deep(.rg-map-pin.selected) { width: 22px; height: 22px; border-width: 4px; box-shadow: 0 2px 10px #1118, 0 0 0 5px #e9653259; }
:deep(.rg-route-endpoint) { display: grid; place-items: center; width: 26px; height: 26px; border: 2px solid #fff; border-radius: 50%; box-shadow: 0 2px 9px #172c3670; color: #fff; font-size: 10px; font-weight: 800; }
:deep(.rg-route-endpoint.start) { background: #28a36b; }
:deep(.rg-route-endpoint.end) { background: #e96532; }
:focus-visible { outline: 2px solid #e96532; outline-offset: 2px; }
.rg-heading, .metrics, .main-grid, .bottom-grid { animation: none; }
.metrics { animation-delay: 40ms; }
.main-grid { animation-delay: 85ms; }
.bottom-grid { animation-delay: 125ms; }
@keyframes reveal { from { opacity: 0; transform: translateY(9px); } to { opacity: 1; transform: translateY(0); } }
@keyframes spin { to { transform: rotate(360deg); } }
@media (max-width: 1280px) { .main-grid { grid-template-columns: minmax(0, 1fr); } .metrics article { padding: 19px 17px; } }
@media (max-width: 1100px) { .main-grid, .bottom-grid { grid-template-columns: 1fr; } .event-list { max-height: 440px; } }
@media (max-width: 1100px) { .map-page .rail-collapsed .map-workspace-body { grid-template-columns: minmax(0, 1fr); }
.map-page .map-workspace-body { grid-template-columns: 1fr; } .map-page .map { height: clamp(480px, 62vh, 760px); } .map-page .map-rail { max-height: none; overflow: visible; } .map-page .device-list { max-height: 340px; overflow: auto; } }
@media (max-width: 760px) { .metrics { grid-template-columns: repeat(2, minmax(0, 1fr)); } .rg-heading { align-items: flex-start; flex-direction: column; gap: 14px; } .heading-actions { justify-content: flex-start; } }
@media (max-width: 600px) { .rg-console { gap: 14px; padding: 6px 0 24px; } .panel { padding: 17px; } .map { height: 330px; } .create-row { grid-template-columns: 1fr; } .device-data small { display: none; } .map-legend { right: 7px; bottom: 7px; gap: 8px; padding: 0 7px; } .map-overlay-top { top: 7px; left: 7px; } .track-stats { grid-template-columns: repeat(2, minmax(0, 1fr)); } .track-stats > div:last-child { grid-column: span 2; } .binding > div { grid-template-columns: 1fr 1fr; padding: 8px 0; } .binding .text-button { grid-column: 2; text-align: left; } }
@media (max-width: 600px) { .map-page .map { height: clamp(380px, 64vh, 560px); } .map-head-actions .tag { display: none; } }
@media (prefers-reduced-motion: reduce) { .rg-heading, .metrics, .main-grid, .bottom-grid, .refresh-button .spinning { animation: none; } .metrics article, .event, .device-row, .actions button, .refresh-button { transition: none; } }

@media (max-width: 992px) {
  .rg-console { gap: 14px; padding: 0 0 16px; }
  .rg-heading { min-height: 0; gap: 10px; flex-wrap: wrap; padding: 0 4px; }
  .rg-heading h1 { font-size: 25px; }.rg-heading p { font-size: 12px; }
  .heading-actions { gap: 10px; }.updated { display: none; }
  .main-grid, .bottom-grid { grid-template-columns: minmax(0, 1fr); }
  .panel { padding: 16px; }.metrics { gap: 10px; }.metrics article { padding: 16px; gap: 14px; }
  .refresh-button, .map-expand, .actions button, .track-ranges button, .track-playback button, .track-close { min-height: 44px; }
  .track-close { min-width: 44px; }.track-playback select { min-height: 44px; }.playback-seek { min-height: 32px; }
  .create-row > *, .binding .el-select { min-width: 0; }
  .binding b { overflow-wrap: anywhere; }.binding .text-button { min-height: 44px; }
  .policy-grid label { min-width: 0; }
  .map-page .rg-heading { flex-direction: row; align-items: center; }
  .map-page .rg-heading h1 { margin: 0; font-size: 21px; }.map-page .rg-heading p { display: none; }
  .map-page .live { font-size: 11px; }.map-page .heading-actions { gap: 8px; }
  .map-page .position-panel { position: relative; padding: 0; height: max(290px, calc(100vh - 208px)); height: max(290px, calc(100dvh - 208px - env(safe-area-inset-top, 0px) - env(safe-area-inset-bottom, 0px))); overflow: hidden; }
  .map-page .position-panel > .panel-head { display: none; }
  .map-page .map-workspace-body { display: block; height: 100%; }
  .map-page .map-stage { height: 100%; border: 0; border-radius: 15px; }
  .map-page .map { height: 100%; min-height: 0; }
  .map-page .map-legend { bottom: 78px; }
  .map-page .map-rail { position: absolute; inset: auto 0 0; z-index: 160; display: flex; flex-direction: column; max-height: 80%; padding: 0; overflow: hidden; border: 1px solid #dfe7e3; border-radius: 18px 18px 0 0; background: #fff; box-shadow: 0 -8px 30px #22383018; }
  .sheet-handle { flex: 0 0 64px; position: relative; display: flex; align-items: center; justify-content: space-between; gap: 10px; width: 100%; padding: 17px 18px 6px; border: 0; background: #fff; color: #273a40; touch-action: none; cursor: pointer; }
  .sheet-tabs { display: grid; grid-template-columns: 1fr 1fr; flex-shrink: 0; gap: 6px; padding: 0 14px 10px; background: #fff; }
  .sheet-tabs button { min-height: 44px; border: 1px solid #e3e9e6; border-radius: 8px; background: #f7f9f8; color: #62737a; font-size: 12px; }
  .sheet-tabs button[aria-pressed="true"] { background: #26383e; border-color: #26383e; color: #fff; }
  .sheet-handle i { position: absolute; top: 8px; left: calc(50% - 18px); width: 36px; height: 4px; border-radius: 3px; background: #c8d2ce; }
  .sheet-handle span { font-weight: 700; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }.sheet-handle small { flex-shrink: 0; color: #6d7d81; }
  .map-page .sheet-content { overflow-y: auto; overscroll-behavior: contain; padding: 0 14px 16px; -webkit-overflow-scrolling: touch; }
  .map-page .track-inspector { padding: 14px; }.map-page .device-list { max-height: none; overflow: visible; flex-shrink: 0; }
  .map-page .device-row { min-height: 64px; }.map-page footer { display: none; }
  .map-page .track-stats { gap: 8px; }.map-page .rider-photo img { height: 120px; }
  .map-page .map-overlay-top { max-width: calc(100% - 24px); font-size: 10px; }
  .map-page .sheet-open .map-legend { display: none; }
}
@media (max-width: 992px) and (max-height: 500px) {
  .map-page .rg-heading { display: none; }
  .map-page .position-panel { height: max(160px, calc(100dvh - 150px - env(safe-area-inset-bottom, 0px))); }
  .map-page .map-rail { max-height: 94%; }.sheet-handle { flex-basis: 48px; }
}
.risk-entry { display: flex; align-items: center; justify-content: space-between; gap: 14px; padding: 18px 20px; border: 1px solid #eddfd5; border-radius: 12px; background: #fff8f3; text-decoration: none; color: #a54b25; }
.risk-entry > div { display: grid; gap: 5px; }.risk-entry strong { font-size: 15px; }.risk-entry span { color: #776d66; font-size: 12px; }.risk-entry > b { font-size: 12px; }
.risk-entry:hover { border-color: #e6ad8b; }
@media (max-width: 600px) { .risk-entry { padding: 14px; flex-wrap: wrap; min-height: 64px; } }
</style>
