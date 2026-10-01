import request from '@/utils/request';
import type { AxiosPromise } from '@/utils/api-types';

export interface Rider { id: number; name: string; phone?: string }
export interface Device {
  device_id: string; rider_id?: number; rider_name?: string; last_seen_ms?: number; last_sample_ms?: number;
  last_lat?: number; last_lng?: number; last_speed?: number; gps_valid: boolean;
  crowd_mode: boolean; last_image_ms?: number; inference_status?: string; inference_mode?: string;
  person_count?: number; latest_image_captured_ms?: number; latest_image_id?: number;
}
export interface Track { id: number; device_id: string; captured_ms: number; lat?: number; lng?: number; speed_kph: number; gps_valid: boolean; heading?: number; gps_accuracy?: number }
export interface TrafficSignal {
  available: boolean; intersectionId?: string; signalGroup?: string; movement: string; state: 'RED' | 'YELLOW' | 'GREEN' | 'UNKNOWN';
  remainingSeconds?: number; distanceM?: number; source: string; mock: boolean; observedAtMs: number; validUntilMs: number;
}
export interface DemoIntersection { enabled: boolean; latitude?: number; longitude?: number; signal?: TrafficSignal }
export interface RiskEvent {
  id: number; device_id: string; rider_name?: string; image_id?: number; inference_mode?: string; person_count?: number; event_type: string;
  crowd_mode: boolean; speed_kph: number | null; speed_limit_kph: number | null; lat?: number; lng?: number;
  captured_ms: number; status: string; intersection_id?: string; signal_state?: string; distance_m?: number;
  signal_source?: string; is_mock?: boolean;
}
export interface Policy { normal_limit_kph: number; crowd_limit_kph: number; crowd_person_count: number }
export interface Overview { onlineDevices: number; riders: number; todayEvents: number; openEvents: number }
export interface EventAction { actor_user_id: number; actor_name: string; old_status: string; new_status: string; note: string; created_at: string }
export interface EventDetail extends RiskEvent { person_count?: number; signal_group?: string; movement?: string; remaining_seconds?: number; observed_ms?: number; expires_ms?: number; actions: EventAction[] }
export interface AiHealth { status: 'UP' | 'DOWN'; mode: string; readyLastHour: number; failedLastHour: number; averageInferenceMs: number; checkedAtMs: number }
export interface DailyEvents { day: string; events: number; pending: number }
export interface Analytics { dailyEvents: DailyEvents[] }

export const getOverview = (): AxiosPromise<Overview> => request({ url: '/riderguard/overview', method: 'get' });
export const getDemoIntersection = (): AxiosPromise<DemoIntersection> => request({ url: '/riderguard/traffic-signal/demo', method: 'get' });
export const getDevices = (): AxiosPromise<Device[]> => request({ url: '/riderguard/devices', method: 'get' });
export const getRiders = (): AxiosPromise<Rider[]> => request({ url: '/riderguard/riders', method: 'get' });
export const addRider = (name: string, phone: string): AxiosPromise<{ id: number }> => request({ url: '/riderguard/riders', method: 'post', data: { name, phone } });
export const addDevice = (deviceId: string, riderId?: number): AxiosPromise<{ deviceId: string; token: string }> => request({ url: '/riderguard/devices', method: 'post', data: { deviceId, riderId } });
export const bindDevice = (deviceId: string, riderId?: number): AxiosPromise<void> => request({ url: `/riderguard/devices/${encodeURIComponent(deviceId)}/rider`, method: 'put', data: { riderId } });
export const rotateDeviceToken = (deviceId: string): AxiosPromise<{ deviceId: string; token: string }> => request({ url: `/riderguard/devices/${encodeURIComponent(deviceId)}/rotate-token`, method: 'post' });
export const getTracks = (deviceId: string, fromMs: number, toMs: number): AxiosPromise<Track[]> => request({ url: `/riderguard/devices/${encodeURIComponent(deviceId)}/tracks`, method: 'get', params: { fromMs, toMs } });
export const getEvents = (): AxiosPromise<RiskEvent[]> => request({ url: '/riderguard/events', method: 'get' });
export const getEventDetail = (id: number): AxiosPromise<EventDetail> => request({ url: `/riderguard/events/${id}`, method: 'get' });
export const processEvent = (id: number, status: string, note: string): AxiosPromise<void> => request({ url: `/riderguard/events/${id}/process`, method: 'put', data: { status, note } });
export const resolveEvent = (id: number): AxiosPromise<void> => request({ url: `/riderguard/events/${id}/resolve`, method: 'put' });
export const getPolicy = (): AxiosPromise<Policy> => request({ url: '/riderguard/policy', method: 'get' });
export const savePolicy = (normalLimitKph: number, crowdLimitKph: number, crowdPersonCount: number): AxiosPromise<void> => request({ url: '/riderguard/policy', method: 'put', data: { normalLimitKph, crowdLimitKph, crowdPersonCount } });
export const getEvidenceImage = (id: number): Promise<Blob> => request({ url: `/riderguard/images/${id}`, method: 'get', responseType: 'blob' }) as Promise<Blob>;
export const getAiHealth = (): AxiosPromise<AiHealth> => request({ url: '/riderguard/ai/health', method: 'get' });
export const getAnalytics = (): AxiosPromise<Analytics> => request({ url: '/riderguard/analytics', method: 'get' });
