<template>
  <section class="event-center">
    <div class="workspace-head">
      <div><h2>风险事件</h2><p>核对速度、位置和现场照片，记录每一步处理结论。</p></div>
      <el-button :loading="loading" @click="loadEvents">刷新事件</el-button>
    </div>
    <div class="filters">
      <el-input v-model="keyword" clearable placeholder="搜索骑手或设备编号" />
      <el-select v-model="typeFilter" aria-label="筛选事件类型">
        <el-option label="全部类型" value="ALL" />
        <el-option label="超速事件" value="OVERSPEED" />
        <el-option label="人群密集风险" value="CROWD_DENSITY" />
        <el-option label="红灯接近提醒" value="RED_SIGNAL_WARNING" />
      </el-select>
      <el-select v-model="statusFilter" aria-label="筛选处理状态">
        <el-option label="全部状态" value="ALL" />
        <el-option label="待处置" value="OPEN" />
        <el-option label="核实中" value="REVIEWING" />
        <el-option label="已处置" value="RESOLVED" />
        <el-option label="误报" value="DISMISSED" />
      </el-select>
      <span>最近 200 条 · {{ filteredEvents.length }} 条符合条件</span>
    </div>
    <div v-if="loadError" class="empty-state" role="status">事件暂时无法更新，已有记录仍保留。请点击刷新重试。</div>
    <div v-if="!filteredEvents.length && !loadError" class="empty-state">{{ loading ? '正在加载事件…' : '当前条件下没有风险事件' }}</div>
    <div v-else class="event-grid" aria-label="风险事件列表">
      <button v-for="event in filteredEvents" :key="event.id" class="event-card" type="button" @click="openEvent(event.id)">
        <span class="event-card-top"><span class="state-dot" :class="event.status.toLowerCase()"></span><b>{{ event.rider_name || event.device_id }}</b><span class="event-type">{{ typeLabel(event.event_type) }}<template v-if="event.is_mock"> · 模拟</template></span><small :class="event.status.toLowerCase()">{{ statusLabel(event.status) }}</small></span>
        <strong v-if="event.event_type === 'CROWD_DENSITY'">{{ event.person_count ?? '—' }} <em>人</em></strong>
        <strong v-else>{{ Number(event.speed_kph).toFixed(1) }} <em>km/h</em></strong>
        <span v-if="event.event_type === 'CROWD_DENSITY'" class="event-meta">单张照片达到人群阈值 · 请核对现场</span>
        <span v-else class="event-meta">{{ event.event_type === 'RED_SIGNAL_WARNING' ? `${event.intersection_id || '未知路口'} · ${event.distance_m == null ? '距离未知' : `${Math.round(event.distance_m)} m`}` : `${event.crowd_mode ? '人群密集区域' : '普通路段'} · 限速 ${Number(event.speed_limit_kph).toFixed(0)} km/h` }}</span>
        <span class="event-meta">{{ formatTime(event.captured_ms) }} · {{ event.image_id ? '有照片证据' : '暂无照片' }}</span>
      </button>
    </div>

    <el-drawer v-model="drawerOpen" class="risk-detail-drawer" append-to-body destroy-on-close :direction="isPhone ? 'btt' : 'rtl'" :size="isPhone ? 'auto' : 'min(760px, 100vw)'" :with-header="false" @close="cancelDetail" @closed="clearPhoto">
      <div class="detail-toolbar"><button class="detail-close" type="button" aria-label="关闭事件详情" @click="drawerOpen = false">关闭 ×</button></div>
      <div v-if="detailLoading" class="detail-feedback" role="status">正在读取事件详情…</div>
      <div v-else-if="detailError" class="detail-feedback" role="alert">暂时无法读取事件详情。<el-button @click="openEvent(requestedEventId)">重试</el-button></div>
      <div v-else-if="detail" class="event-detail">
        <header><div><span class="eyebrow">EVENT / {{ detail.id }}<template v-if="detail.is_mock"> · MOCK / 模拟数据</template></span><h2>{{ detail.event_type === 'CROWD_DENSITY' ? '人群密集风险' : detail.event_type === 'RED_SIGNAL_WARNING' ? '红灯接近提醒' : detail.crowd_mode ? '人群密集区域超速' : '超速事件' }}</h2><p>{{ detail.rider_name || detail.device_id }} · {{ formatTime(detail.captured_ms) }}</p></div><span class="status-tag" :class="detail.status.toLowerCase()">{{ statusLabel(detail.status) }}</span></header>
        <div v-if="detail.event_type === 'CROWD_DENSITY'" class="detail-stats"><div><small>前方人数</small><strong>{{ detail.person_count ?? '—' }} <em>人</em></strong></div><div><small>触发条件</small><strong>单帧达到阈值</strong></div><div><small>车速</small><strong>暂无数据</strong></div></div>
        <div v-else class="detail-stats"><div><small>事发车速</small><strong>{{ Number(detail.speed_kph).toFixed(1) }} <em>km/h</em></strong></div><div><small>{{ detail.event_type === 'RED_SIGNAL_WARNING' ? '路口' : '当时限速' }}</small><strong>{{ detail.event_type === 'RED_SIGNAL_WARNING' ? detail.intersection_id || '—' : Number(detail.speed_limit_kph).toFixed(1) }} <em>{{ detail.event_type === 'RED_SIGNAL_WARNING' ? '' : 'km/h' }}</em></strong></div><div><small>{{ detail.event_type === 'RED_SIGNAL_WARNING' ? '距离路口' : '前方人数' }}</small><strong>{{ detail.event_type === 'RED_SIGNAL_WARNING' ? detail.distance_m == null ? '—' : Math.round(detail.distance_m) : detail.person_count ?? '—' }} <em>{{ detail.event_type === 'RED_SIGNAL_WARNING' ? 'm' : '人' }}</em></strong></div></div>
        <div v-if="detail.event_type === 'RED_SIGNAL_WARNING'" class="detail-section"><h3>信号快照</h3><p class="fine-print">{{ detail.intersection_id }} · {{ detail.signal_group }} · {{ detail.movement }} · {{ detail.signal_state }} · 来源 {{ detail.signal_source }}。<template v-if="detail.is_mock">这是联调数据，不代表真实道路交通信号。</template></p></div>
        <div class="detail-section"><div class="section-title"><h3>现场证据</h3><el-button text type="primary" @click="goToMap">查看地图与轨迹</el-button></div>
          <img v-if="photoUrl" :src="photoUrl" class="photo" alt="事件现场照片" />
          <div v-else class="photo-empty">{{ detail.image_id ? '照片加载中或暂不可用' : '此事件暂无关联照片' }}</div>
          <p class="fine-print">{{ detail.lat == null ? '事发时 GPS 无有效定位' : `原始 GPS：${Number(detail.lat).toFixed(6)}, ${Number(detail.lng).toFixed(6)}` }} · 识别来源：{{ detail.inference_mode || '未关联' }}</p>
        </div>
        <div v-if="canManage && (detail.status === 'OPEN' || detail.status === 'REVIEWING')" class="detail-section action-form"><h3>记录处置</h3><el-select v-model="nextStatus" aria-label="处置结论"><el-option label="核实中" value="REVIEWING" /><el-option label="确认并处置" value="RESOLVED" /><el-option label="判定误报" value="DISMISSED" /></el-select><el-input v-model="note" type="textarea" :rows="3" maxlength="500" show-word-limit placeholder="填写核实经过和处理结论" /><el-button type="primary" :loading="saving" @click="saveAction">保存处置记录</el-button></div>
        <div class="detail-section"><h3>处置记录</h3><div v-if="!detail.actions?.length" class="history-empty">尚无处置记录</div><ol v-else class="history"><li v-for="(action, index) in detail.actions" :key="index"><b>{{ statusLabel(action.new_status) }}</b><span>{{ action.actor_name }} · {{ formatTime(action.created_at) }}</span><p>{{ action.note }}</p></li></ol></div>
      </div>
    </el-drawer>
  </section>
</template>

<script setup lang="ts">
import { ElMessage } from 'element-plus';
import { getEventDetail, getEvents, getEvidenceImage, processEvent } from '@/api/riderguard';
import type { EventDetail, RiskEvent } from '@/api/riderguard';
import { useUserStore } from '@/store/modules/user';

const { width: viewportWidth } = useWindowSize();
const isPhone = computed(() => viewportWidth.value <= 992);
const route = useRoute();
const router = useRouter();
const roles = useUserStore().roles;
const canManage = computed(() => roles.includes('superadmin') || roles.includes('riderguard_manager'));
const events = ref<RiskEvent[]>([]);
const detail = ref<EventDetail>();
const detailLoading = ref(false), detailError = ref(false), requestedEventId = ref(0);
const keyword = ref('');
const statusFilter = ref('ALL');
const typeFilter = ref('ALL');
const loading = ref(false);
const loadError = ref(false);
const saving = ref(false);
const drawerOpen = ref(false);
const nextStatus = ref('REVIEWING');
const note = ref('');
const photoUrl = ref('');
let detailRequestVersion = 0;
const filteredEvents = computed(() => events.value.filter(event =>
  (typeFilter.value === 'ALL' || event.event_type === typeFilter.value) &&
  (statusFilter.value === 'ALL' || event.status === statusFilter.value) &&
  `${event.rider_name || ''} ${event.device_id}`.toLowerCase().includes(keyword.value.trim().toLowerCase())
));
const statusLabel = (status: string) => ({ OPEN: '待处置', REVIEWING: '核实中', RESOLVED: '已处置', DISMISSED: '误报' })[status as 'OPEN'] || status;
const typeLabel = (type: string) => ({ OVERSPEED: '超速', CROWD_DENSITY: '人群密集', RED_SIGNAL_WARNING: '红灯接近' })[type as 'OVERSPEED'] || type;
const formatTime = (value: number | string) => new Date(value).toLocaleString('zh-CN', { hour12: false });

async function loadEvents() {
  if (loading.value) return;
  loading.value = true;
  try { events.value = (await getEvents()).data || []; loadError.value = false; }
  catch { loadError.value = true; }
  finally { loading.value = false; }
}
function clearPhoto() { if (photoUrl.value) URL.revokeObjectURL(photoUrl.value); photoUrl.value = ''; }
function cancelDetail() { detailRequestVersion++; detailLoading.value = false; clearPhoto(); }
async function openEvent(id: number) {
  const version = ++detailRequestVersion;
  requestedEventId.value = id;
  clearPhoto(); detail.value = undefined;
  detailError.value = false; detailLoading.value = true; drawerOpen.value = true;
  try {
    const loaded = (await getEventDetail(id)).data;
    if (version !== detailRequestVersion) return;
    if (!loaded) { detailError.value = true; return; }
    detail.value = loaded;
    nextStatus.value = loaded.status === 'REVIEWING' ? 'RESOLVED' : 'REVIEWING';
    note.value = '';
    detailLoading.value = false;
    if (loaded.image_id) {
      try {
        const blob = await getEvidenceImage(loaded.image_id);
        if (version === detailRequestVersion && drawerOpen.value) photoUrl.value = URL.createObjectURL(blob);
      } catch { if (version === detailRequestVersion) ElMessage.warning('现场照片暂不可用'); }
    }
  } catch { if (version === detailRequestVersion) detailError.value = true; }
  finally { if (version === detailRequestVersion) detailLoading.value = false; }
}
async function saveAction() {
  if (!detail.value || !note.value.trim()) { ElMessage.warning('请填写处置说明'); return; }
  const eventId = detail.value.id;
  const version = detailRequestVersion;
  saving.value = true;
  try {
    await processEvent(eventId, nextStatus.value, note.value.trim());
    ElMessage.success('处置记录已保存');
    await loadEvents();
    if (drawerOpen.value && version === detailRequestVersion) await openEvent(eventId);
  } finally { saving.value = false; }
}
function goToMap() {
  if (!detail.value) return;
  void router.push({ path: '/riderguard/map', query: { device: detail.value.device_id, event: String(detail.value.id), at: String(detail.value.captured_ms), lat: detail.value.lat ?? undefined, lng: detail.value.lng ?? undefined } });
}
watch(() => route.query.event, value => { const id = Number(value); if (Number.isInteger(id) && id > 0) void openEvent(id); }, { immediate: true });
let refreshTimer: ReturnType<typeof setInterval> | undefined;
onMounted(() => { void loadEvents(); refreshTimer = setInterval(() => { if (!document.hidden && !loading.value) void loadEvents(); }, 15000); });
onUnmounted(() => { if (refreshTimer) clearInterval(refreshTimer); detailRequestVersion++; clearPhoto(); });
</script>

<style scoped lang="scss">
.event-center { display: grid; gap: 18px; }
.workspace-head, .filters, .event-card-top, .event-detail header, .section-title { display: flex; align-items: center; justify-content: space-between; gap: 16px; }
.workspace-head { padding: 8px 3px 16px; color: #202d34; }
.workspace-head h2, .event-detail h2 { margin: 5px 0; font-size: 26px; letter-spacing: -.035em; }
.workspace-head p, .event-detail header p { margin: 0; color: #66767d; font-size: 13px; }
.eyebrow { color: #f18753; font-size: 10px; font-weight: 800; letter-spacing: .16em; }
.filters { justify-content: flex-start; padding: 16px; border: 1px solid #e5eae9; border-radius: 12px; background: #fff; }
.filters .el-input { max-width: 300px; }.filters .el-select { width: 150px; }.filters span { margin-left: auto; color: #8a989e; font-size: 12px; }
.event-grid { display: grid; gap: 8px; }
.event-card { display: grid; grid-template-columns: minmax(230px, 1.3fr) 115px minmax(190px, 1fr) minmax(230px, 1.2fr); align-items: center; gap: 18px; padding: 18px 20px; border: 1px solid #e6ecea; border-radius: 13px; background: #fff; text-align: left; cursor: pointer; transition: transform 160ms ease, border-color 160ms ease, box-shadow 160ms ease; }
.event-card:hover { transform: translateX(2px); border-color: #efb497; box-shadow: 0 12px 28px #22353c13; }
.event-card-top { justify-content: flex-start; }.event-card-top b { color: #283941; font-size: 13px; }.event-card-top small { margin-left: auto; color: #c86639; font-size: 11px; }
.event-card-top small.resolved, .event-card-top small.dismissed { color: #5b8d77; }
.event-type { padding: 3px 6px; border-radius: 5px; background: #f2f5f3; color: #687b7d; font-size: 10px; white-space: nowrap; }
.state-dot { width: 8px; height: 8px; border-radius: 50%; background: #e7783f; }.state-dot.resolved, .state-dot.dismissed { background: #6fa389; }
.event-card strong { color: #24343c; font-size: 23px; font-variant-numeric: tabular-nums; }.event-card strong em, .detail-stats em { color: #8d9b9f; font-size: 12px; font-style: normal; font-weight: 500; }
.event-meta { color: #66767d; font-size: 12px; }.empty-state, .history-empty { padding: 35px; border: 1px dashed #d7e0dc; border-radius: 12px; color: #819198; text-align: center; }
.event-detail { display: grid; gap: 22px; padding: 12px; color: #23333c; }.event-detail header { align-items: flex-start; }.event-detail header p { color: #849198; }
.status-tag { padding: 6px 10px; border-radius: 6px; background: #fff1e9; color: #bb6237; font-size: 11px; white-space: nowrap; }.status-tag.resolved, .status-tag.dismissed { background: #eaf5ee; color: #3d7d5d; }
.detail-stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 9px; }.detail-stats div { display: grid; gap: 8px; padding: 16px; border-radius: 10px; background: #f3f6f5; }.detail-stats small { color: #7d8b91; }.detail-stats strong { font-size: 23px; font-variant-numeric: tabular-nums; }
.detail-section { display: grid; gap: 12px; padding-top: 20px; border-top: 1px solid #e6ebe9; }.detail-section h3 { margin: 0; font-size: 15px; }.photo { width: 100%; max-height: 410px; object-fit: contain; border-radius: 9px; background: #eaf0ee; }.photo-empty { display: grid; place-items: center; min-height: 64px; padding: 16px; border-radius: 9px; background: #eef3f1; color: #87979b; }.fine-print { margin: 0; color: #8b999e; font-size: 11px; }
.action-form .el-select { width: 190px; }.action-form .el-button { justify-self: start; }.history { display: grid; gap: 14px; margin: 0; padding-left: 18px; }.history li { padding-left: 6px; }.history b { color: #bd6239; }.history span { margin-left: 10px; color: #8c999f; font-size: 11px; }.history p { margin: 6px 0 0; font-size: 12px; line-height: 1.6; }
@media (max-width: 1200px) { .event-card { grid-template-columns: minmax(0, 1fr) auto; gap: 12px; }.event-card-top { min-width: 0; }.event-card-top b { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }.event-meta { line-height: 1.6; } }
@media (max-width: 640px) { .workspace-head, .filters { flex-wrap: wrap; }.filters .el-input { max-width: none; }.filters span { margin-left: 0; }.detail-stats { grid-template-columns: 1fr; } }
@media (prefers-reduced-motion: reduce) { .event-card { transition: none; } }
.detail-toolbar { position: sticky; top: -16px; z-index: 3; display: flex; justify-content: flex-end; padding: 6px 0 10px; background: #fff; }
.detail-feedback { display: grid; justify-items: center; gap: 16px; padding: 40px 12px; color: #687a81; font-size: 14px; }
.detail-close { justify-self: end; min-height: 44px; padding: 0 14px; border: 1px solid #dce5e0; border-radius: 8px; background: #fff; color: #34474c; cursor: pointer; }
@media (max-width: 992px) {
  .event-detail { padding: 0 0 calc(16px + env(safe-area-inset-bottom, 0px)); gap: 16px; }
  .event-detail header { flex-wrap: wrap; }.event-detail h2 { font-size: 23px; }
  .detail-stats { grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 6px; }.detail-stats div { padding: 10px; }.detail-stats strong { font-size: 20px; }
  .filters { gap: 10px; }.filters .el-select { width: 100%; }.filters > span { margin-left: 0; }
  .action-form .el-select, .action-form .el-button { width: 100%; }
  .history span { display: block; margin: 5px 0; }.fine-print { overflow-wrap: anywhere; }
}
</style>

<style lang="scss">
/* Teleported drawer: size to content instead of reserving an empty screen. */
.el-drawer.risk-detail-drawer {
  height: auto;
  max-height: calc(100vh - 24px);
  max-height: calc(100dvh - 24px);
}
.el-drawer.risk-detail-drawer.rtl { top: 12px; bottom: auto; border-radius: 16px 0 0 16px; }
.el-drawer.risk-detail-drawer.btt {
  top: auto; bottom: 0;
  max-height: 92vh;
  max-height: 92dvh;
  border-radius: 18px 18px 0 0;
}
.risk-detail-drawer .el-drawer__body { flex: 0 1 auto; min-height: 0; overflow-y: auto; overscroll-behavior: contain; }
</style>
