import { describe, expect, it } from 'vitest';
import type { Track } from '@/api/riderguard';
import { splitTracks, validFix } from './riderguard-track';

const point = (id: number, extra: Partial<Track> = {}): Track => ({
  id, device_id: 'test', captured_ms: id * 1000, lat: 34, lng: 108 + id * 0.00001,
  gps_valid: true, speed_kph: 12, ...extra
});

describe('GPS route continuity', () => {
  it('orders late uploads by capture time without mutating input', () => {
    const input = [point(3), point(1), point(2)];
    expect(splitTracks(input).map(segment => segment.map(p => p.id))).toEqual([[1, 2, 3]]);
    expect(input[0].id).toBe(3);
  });
  it('does not bridge invalid fixes or a reporting gap', () => {
    const input = [point(1), point(2, { gps_valid: false }), point(3), point(4, { captured_ms: 30000 })];
    expect(splitTracks(input).map(segment => segment.map(p => p.id))).toEqual([[1], [3], [4]]);
  });
  it('isolates a GPS spike and reconnects subsequent normal points', () => {
    const input = [point(1), point(2), point(3, { lng: 110 }), point(4), point(5)];
    expect(splitTracks(input).map(segment => segment.map(p => p.id))).toEqual([[1, 2], [3], [4, 5]]);
  });
  it('rejects missing and out of range coordinates while allowing the equator', () => {
    expect(validFix(point(1, { lat: undefined }))).toBe(false);
    expect(validFix(point(1, { lat: 91 }))).toBe(false);
    expect(validFix(point(1, { lng: NaN }))).toBe(false);
    expect(validFix(point(1, { lat: 0 }))).toBe(true);
  });
  it('does not draw movement between two positions with identical timestamps', () => {
    expect(splitTracks([point(1), point(2, { captured_ms: 1000 })])).toHaveLength(2);
  });
});
