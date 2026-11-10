<template>
  <section class="supervision">
    <header class="panel-head"><div><h2>企业监管</h2><p>急刹留证、超速累计上报与路口人工复核</p></div><el-button :loading="loading" @click="reload">刷新</el-button></header>
    <el-alert v-if="error" :title="error" type="error" show-icon :closable="false" />
    <el-alert title="急刹属于安全事件；路口抓拍属于待核查线索。系统不依据单张红灯照片自动认定违规。" type="info" :closable="false" />
    <el-tabs v-model="tab">
      <el-tab-pane label="事件复核" name="events">
        <div class="filters"><el-select v-model="kind"><el-option label="全部事件" value="ALL" /><el-option label="急刹抓拍" value="HARSH_BRAKE" /><el-option label="超速记录" value="OVERSPEED" /><el-option label="路口待核查" value="INTERSECTION_REVIEW" /></el-select>
          <el-select v-model="status"><el-option label="全部状态" value="ALL" /><el-option v-for="(label, value) in statuses" :key="value" :label="label" :value="value" /></el-select>
          <el-switch v-model="mock" active-text="模拟演练" inactive-text="真实设备" @change="loadEvents" /><span>最近200条 · {{ filtered.length }}条</span>
        </div>
        <el-table :data="filtered" @row-click="openDetail" empty-text="暂无符合条件的监管事件">
          <el-table-column label="骑手 / 设备" min-width="160"><template #default="{row}">{{ row.rider_name || row.device_id }}</template></el-table-column>
          <el-table-column label="事件" min-width="150"><template #default="{row}">{{ types[row.event_type] }}<el-tag v-if="row.is_mock" type="info" size="small">模拟</el-tag></template></el-table-column>
          <el-table-column label="发生时间" min-width="180"><template #default="{row}">{{ time(row.captured_ms) }}</template></el-table-column>
          <el-table-column label="触发依据" min-width="180"><template #default="{row}">{{ basis(row) }}</template></el-table-column>
          <el-table-column label="照片" width="80" prop="photo_count" />
          <el-table-column label="状态" width="110"><template #default="{row}">{{ statuses[row.status] || row.status }}</template></el-table-column>
          <el-table-column label="" width="90"><template #default="{row}"><el-button link type="primary" @click.stop="openDetail(row)">查看</el-button></template></el-table-column>
        </el-table>
      </el-tab-pane>
      <el-tab-pane label="提醒与累计规则" name="policy">
        <el-form v-if="policy" label-width="170px" class="policy-form" :disabled="!canManage || saving">
          <el-form-item label="启用监管采集"><el-switch v-model="policy.enabled" /></el-form-item>
          <el-form-item label="配置超速限值"><el-switch v-model="hasLimit" /><span class="hint">未配置时只采集，不判断超速</span></el-form-item>
          <el-form-item v-if="hasLimit" label="限速 km/h"><el-input-number v-model="policy.speedLimitKph" :min="1" :max="80" :precision="1" /></el-form-item>
          <el-form-item label="连续超速时间 s"><el-input-number v-model="speedHold" :min="1" :max="30" /></el-form-item>
          <el-form-item label="恢复正常时间 s"><el-input-number v-model="recovery" :min="1" :max="30" /></el-form-item>
          <el-form-item label="累计窗口 min"><el-input-number v-model="windowMinutes" :min="1" :max="60" /></el-form-item>
          <el-form-item label="上报门槛 次"><el-input-number v-model="policy.countThreshold" :min="2" :max="32" /></el-form-item>
          <el-form-item label="急刹减速度 m/s²"><el-input-number v-model="policy.brakeThresholdMs2" :min="1" :max="12" :precision="1" /></el-form-item>
          <el-form-item label="急刹持续时间 ms"><el-input-number v-model="policy.brakeHoldMs" :min="100" :max="2000" :step="50" /></el-form-item>
          <el-form-item label="传感器车头方向"><el-select v-model="policy.forwardAxis"><el-option v-for="a in axes" :key="a.value" :label="a.label" :value="a.value" /></el-select></el-form-item>
          <el-form-item><el-button type="primary" :loading="saving" @click="savePolicy">保存规则</el-button><span class="hint">设备下次同步后生效 · 当前版本 {{ policy.version }}</span></el-form-item>
        </el-form>
      </el-tab-pane>
      <el-tab-pane label="试验路口" name="roads">
        <p class="hint">在原始GNSS坐标系中配置接近区域。进入区域仅触发连续快照，不代表已越过停止线。</p>
        <div class="road" v-for="(road,index) in roads" :key="road.id">
          <el-input v-model="road.name" placeholder="路口名称" :disabled="!canManage" />
          <el-input-number v-model="road.latitude" :min="-90" :max="90" :precision="7" :controls="false" aria-label="纬度" />
          <el-input-number v-model="road.longitude" :min="-180" :max="180" :precision="7" :controls="false" aria-label="经度" />
          <el-input-number v-model="road.radiusM" :min="10" :max="300" aria-label="接近半径 米" />
          <el-input v-model="road.movement" placeholder="行驶方向，如直行" /><el-input v-model="road.signalGroup" placeholder="适用灯组说明" />
          <el-switch v-model="road.enabled" :disabled="!canManage" /><el-button v-if="canManage" @click="roads.splice(index,1)">移除</el-button>
        </div>
        <div class="filters" v-if="canManage"><el-button :disabled="roads.length >= 8" @click="addRoad">添加路口</el-button><el-button type="primary" :loading="saving" @click="saveRoads">保存路口</el-button></div>
      </el-tab-pane>
    </el-tabs>
    <el-drawer v-model="drawer" title="事件证据与复核" size="min(820px, 100vw)" @close="closeDetail">
      <div v-if="detail" class="evidence">
        <el-tag v-if="detail.is_mock" type="warning">模拟演练记录，不计入真实监管统计</el-tag>
        <h3>{{ types[detail.event_type] }} · {{ detail.device_id }}</h3>
        <el-descriptions :column="2" border><el-descriptions-item label="发生时间">{{ time(detail.captured_ms) }}</el-descriptions-item><el-descriptions-item label="接收时间">{{ time(detail.received_ms) }}</el-descriptions-item>
          <el-descriptions-item label="触发依据">{{ basis(detail) }}</el-descriptions-item><el-descriptions-item label="定位">{{ detail.gps_valid ? `${detail.lat}, ${detail.lng}` : '未取得有效GPS' }}</el-descriptions-item>
          <el-descriptions-item label="记录时长">{{ (detail.duration_ms / 1000).toFixed(1) }} s</el-descriptions-item><el-descriptions-item label="证据完整性">{{ detail.partial_evidence ? '包含数据中断，需审慎复核' : '未报告数据中断' }}</el-descriptions-item>
        </el-descriptions>
        <div v-if="photos.length" class="photos"><figure v-for="photo in photos" :key="photo.id"><img :src="photo.url" alt="事件现场原始照片" /><figcaption>{{ time(photo.at) }}</figcaption></figure></div>
        <el-empty v-else description="暂无现场照片；不能以此推断现场正常" :image-size="60" />
        <el-table v-if="trace.length" :data="trace" max-height="220" size="small"><el-table-column label="相对时间 ms" prop="offsetMs" /><el-table-column label="纵向加速度 m/s²" prop="forwardMs2" /><el-table-column label="垂向加速度 m/s²" prop="verticalMs2" /><el-table-column label="角速度 °/s" prop="gyroDps" /></el-table>
        <el-alert v-if="detail.event_type === 'HARSH_BRAKE'" title="确认急刹表示事件属实，不等于认定骑手违规。" :closable="false" />
        <el-form v-if="canManage" label-position="top"><el-form-item label="人工复核结果"><el-select v-model="review"><el-option label="确认事件" value="CONFIRMED" /><el-option label="驳回 / 误触发" value="DISMISSED" /><el-option label="证据不足" value="INSUFFICIENT" /></el-select></el-form-item><el-form-item label="复核说明"><el-input v-model="note" type="textarea" maxlength="1000" show-word-limit :rows="3" /></el-form-item><el-button type="primary" :loading="saving" @click="saveReview">提交复核</el-button></el-form>
        <ol class="reviews"><li v-for="(r,i) in detail.reviews" :key="i">{{ r.actor_name }} · {{ statuses[r.new_status] || r.new_status }} · {{ time(r.created_ms) }}<p>{{ r.note }}</p></li></ol>
      </div><el-skeleton v-else :rows="8" animated />
    </el-drawer>
  </section>
</template>
<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { ElMessage } from 'element-plus';
import { useUserStore } from '@/store/modules/user';
import { getSupervisionPolicy, saveSupervisionPolicy, getTrialIntersections, saveTrialIntersections, getSafetyEvents, getSafetyEvent, reviewSafetyEvent, getSafetyPhoto } from '@/api/riderguard/supervision';
import type { SupervisionPolicy, TrialIntersection, SafetyEvent } from '@/api/riderguard/supervision';
const types: Record<string,string> = { HARSH_BRAKE:'急刹抓拍', OVERSPEED:'超速记录', INTERSECTION_REVIEW:'路口待核查' };
const statuses: Record<string,string> = { PENDING:'待复核', CONFIRMED:'已确认', DISMISSED:'已驳回', INSUFFICIENT:'证据不足' };
const axes = [{label:'+X朝前',value:1},{label:'−X朝前',value:-1},{label:'+Y朝前',value:2},{label:'−Y朝前',value:-2},{label:'+Z朝前',value:3},{label:'−Z朝前',value:-3}];
const canManage = computed(() => useUserStore().roles.some(r => ['superadmin','riderguard_manager'].includes(r)));
const tab=ref('events'), kind=ref('ALL'), status=ref('ALL'), mock=ref(false), error=ref(''), loading=ref(false), saving=ref(false);
const policy=ref<SupervisionPolicy>(), roads=ref<TrialIntersection[]>([]), events=ref<SafetyEvent[]>([]), detail=ref<SafetyEvent>(), drawer=ref(false);
const hasLimit=ref(false), review=ref('INSUFFICIENT'), note=ref(''), photos=ref<{id:number;url:string;at:number|null}[]>([]);
const filtered=computed(()=>events.value.filter(e=>(kind.value==='ALL'||e.event_type===kind.value)&&(status.value==='ALL'||e.status===status.value)));
const speedHold=computed({get:()=> (policy.value?.speedingHoldMs||3000)/1000,set:v=>{if(policy.value)policy.value.speedingHoldMs=v*1000;}});
const recovery=computed({get:()=> (policy.value?.recoveryMs||5000)/1000,set:v=>{if(policy.value)policy.value.recoveryMs=v*1000;}});
const windowMinutes=computed({get:()=> (policy.value?.windowMs||600000)/60000,set:v=>{if(policy.value)policy.value.windowMs=v*60000;}});
const trace=computed(()=>{try {const t=JSON.parse(detail.value?.payload_json||'{}').trace; return Array.isArray(t)?t:[];}catch{return [];}});
const time=(n:number|null|undefined)=>n?new Date(n).toLocaleString('zh-CN',{hour12:false}):'设备时间未同步';
const basis=(e:SafetyEvent)=>e.event_type==='HARSH_BRAKE'?`纵向 ${e.min_forward?.toFixed(2) ?? '—'} m/s²`:e.event_type==='OVERSPEED'?`最高 ${e.peak_speed?.toFixed(1) ?? '—'} / 限速 ${e.speed_limit ?? '—'} km/h`:`试验路口 ${e.intersection_id ?? '—'}`;
let detailVersion=0, timer:ReturnType<typeof setInterval>|undefined;
async function loadEvents(){try{events.value=(await getSafetyEvents(mock.value)).data||[];}catch{error.value='监管事件暂时无法更新，请重试。';}}
async function reload(){if(loading.value)return;loading.value=true;error.value='';try{const [p,r]=await Promise.all([getSupervisionPolicy(),getTrialIntersections()]);policy.value=p.data;hasLimit.value=policy.value?.speedLimitKph!=null;roads.value=r.data||[];await loadEvents();}catch{error.value='监管配置暂不可用，请检查后台连接。';}finally{loading.value=false;}}
async function savePolicy(){if(!policy.value)return;saving.value=true;try{policy.value=(await saveSupervisionPolicy({...policy.value,speedLimitKph:hasLimit.value?policy.value.speedLimitKph:null})).data;ElMessage.success('规则已保存，等待设备同步');}finally{saving.value=false;}}
function addRoad(){const id=Math.max(0,...roads.value.map(r=>r.id))+1;roads.value.push({id,name:'',latitude:0,longitude:0,radiusM:60,movement:'直行',signalGroup:'',enabled:false});}
async function saveRoads(){saving.value=true;try{roads.value=(await saveTrialIntersections(roads.value)).data;ElMessage.success('试验路口已保存');}finally{saving.value=false;}}
function closeDetail(){detailVersion++;photos.value.forEach(p=>URL.revokeObjectURL(p.url));photos.value=[];detail.value=undefined;}
async function openDetail(row:SafetyEvent){closeDetail();const version=detailVersion;drawer.value=true;try{const result=(await getSafetyEvent(row.id)).data;if(version!==detailVersion)return;detail.value=result;review.value='INSUFFICIENT';note.value='';for(const p of result.photos||[]){try{const blob=await getSafetyPhoto(p.id);if(version!==detailVersion)return;photos.value.push({id:p.id,url:URL.createObjectURL(blob),at:p.captured_ms});}catch{ElMessage.warning('部分证据照片暂不可用');}}}catch{if(version===detailVersion){drawer.value=false;ElMessage.error('事件详情读取失败');}}}
async function saveReview(){if(!detail.value||!note.value.trim()){ElMessage.warning('请填写复核说明');return;}const id=detail.value.id;saving.value=true;try{await reviewSafetyEvent(id,review.value,note.value.trim());ElMessage.success('复核已保存');await loadEvents();await openDetail({...detail.value,id});}finally{saving.value=false;}}
onMounted(()=>{void reload();timer=setInterval(()=>{if(!document.hidden&&tab.value==='events')void loadEvents();},15000);});
onUnmounted(()=>{if(timer)clearInterval(timer);closeDetail();});
</script>
<style scoped>
.supervision{display:grid;gap:16px;color:#263941}.panel-head,.filters{display:flex;align-items:center;justify-content:space-between;gap:12px}.panel-head h2{margin:0 0 6px;font-size:25px}.panel-head p,.hint{color:#75868c;font-size:13px}.filters{justify-content:flex-start;flex-wrap:wrap;margin:12px 0}.filters .el-select{width:155px}.filters>span{margin-left:auto;color:#75868c;font-size:12px}.policy-form{max-width:750px;padding:18px;background:white;border:1px solid #e5eae9;border-radius:12px}.hint{margin-left:12px}.road{display:grid;grid-template-columns:1.1fr 1fr 1fr 150px 1fr 1fr auto auto;gap:8px;margin:12px 0}.evidence{display:grid;gap:18px}.photos{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.photos figure{margin:0}.photos img{width:100%;height:200px;object-fit:contain;background:#eef3f1;border-radius:8px}.photos figcaption{margin-top:6px;color:#75868c;font-size:12px}.reviews{padding-left:20px;font-size:13px}.reviews p{color:#75868c}@media(max-width:1000px){.road{grid-template-columns:1fr 1fr}.panel-head{align-items:flex-start}.photos{grid-template-columns:1fr}.hint{display:block;margin-left:0}.filters>span{margin-left:0}}
</style>
