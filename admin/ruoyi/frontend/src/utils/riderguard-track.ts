import type { Track } from '@/api/riderguard';

export function validFix(point: Track): boolean {
  return Boolean(point.gps_valid) && point.lat != null && point.lng != null &&
    Number.isFinite(Number(point.lat)) && Number.isFinite(Number(point.lng)) &&
    Math.abs(Number(point.lat)) <= 90 && Math.abs(Number(point.lng)) <= 180;
}

export function distanceMeters(a: number[], b: number[]): number {
  const radians = Math.PI / 180;
  const dLat = (b[1] - a[1]) * radians;
  const dLng = (b[0] - a[0]) * radians;
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(a[1] * radians) * Math.cos(b[1] * radians) * Math.sin(dLng / 2) ** 2;
  return 12742000 * Math.asin(Math.sqrt(Math.min(1, h)));
}

// These are display continuity limits, not speed policy or event detection rules.
export function splitTracks(points: Track[]): Track[][] {
  const segments: Track[][] = [];
  let segment: Track[] = [];
  for (const point of [...points].sort((a, b) => a.captured_ms - b.captured_ms || a.id - b.id)) {
    if (!validFix(point)) { segment = []; continue; }
    const previous = segment[segment.length - 1];
    if (previous) {
      const elapsed = point.captured_ms - previous.captured_ms;
      const distance = distanceMeters([Number(previous.lng), Number(previous.lat)], [Number(point.lng), Number(point.lat)]);
      if (elapsed <= 0 || elapsed > 15000 || distance > Math.max(40, elapsed / 1000 * 100 / 3.6)) segment = [];
    }
    if (!segment.length) segments.push(segment);
    segment.push(point);
  }
  return segments;
}
