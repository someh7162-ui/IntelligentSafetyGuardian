import request from '@/utils/request';
import type { AxiosPromise } from '@/utils/api-types';
export interface SupervisionPolicy {
  version: number; enabled: boolean; speedLimitKph: number | null; speedingHoldMs: number; recoveryMs: number;
  windowMs: number; countThreshold: number; brakeThresholdMs2: number; brakeHoldMs: number; forwardAxis: number;
  gpsFreshMs: number; telemetryIntervalMs: number; approachPhotos: number; photoIntervalMs: number;
}
export interface TrialIntersection { id: number; name: string; latitude: number; longitude: number; radiusM: number; movement: string; signalGroup: string; enabled: boolean }
export interface SafetyEvent {
  id: number; device_id: string; rider_name?: string; event_id: string; event_type: 'HARSH_BRAKE' | 'OVERSPEED' | 'INTERSECTION_REVIEW';
  captured_ms: number | null; received_ms: number; gps_valid: boolean; lat: number | null; lng: number | null;
  peak_speed: number | null; speed_limit: number | null; duration_ms: number; min_forward: number | null;
  intersection_id?: number; is_mock: boolean; partial_evidence: boolean; status: string; photo_count?: number; payload_json: string;
  photos?: { id: number; photo_id: string; captured_ms: number | null; content_hash: string }[];
  reviews?: { actor_name: string; old_status: string; new_status: string; note: string; created_ms: number }[];
}
const base = '/riderguard/supervision';
export const getSupervisionPolicy = (): AxiosPromise<SupervisionPolicy> => request({ url: `${base}/policy` });
export const saveSupervisionPolicy = (data: SupervisionPolicy): AxiosPromise<SupervisionPolicy> => request({ url: `${base}/policy`, method: 'put', data });
export const getTrialIntersections = (): AxiosPromise<TrialIntersection[]> => request({ url: `${base}/intersections` });
export const saveTrialIntersections = (data: TrialIntersection[]): AxiosPromise<TrialIntersection[]> => request({ url: `${base}/intersections`, method: 'put', data });
export const getSafetyEvents = (mock = false): AxiosPromise<SafetyEvent[]> => request({ url: `${base}/events`, params: { mock } });
export const getSafetyEvent = (id: number): AxiosPromise<SafetyEvent> => request({ url: `${base}/events/${id}` });
export const reviewSafetyEvent = (id: number, status: string, note: string): AxiosPromise<void> => request({ url: `${base}/events/${id}/review`, method: 'put', data: { status, note } });
export const getSafetyPhoto = (id: number): Promise<Blob> => request({ url: `${base}/images/${id}`, responseType: 'blob' }) as Promise<Blob>;
