/**
 * platform.ts — Единый определитель среды выполнения NanoDecompiler (DRY).
 * Предотвращает дублирующиеся проверки navigator.userAgent, window.nano и window.__TAURI__.
 */

export type PlatformType = "electron" | "tauri" | "android" | "web";

export function isElectron(): boolean {
  if (typeof window === "undefined") return false;
  const w = window as any;
  return Boolean(
    w.nano?.isElectron ||
    w.process?.type === "renderer" ||
    (typeof navigator !== "undefined" && navigator.userAgent.toLowerCase().includes("electron"))
  );
}

export function isTauri(): boolean {
  if (typeof window === "undefined") return false;
  const w = window as any;
  return Boolean(w.__TAURI__ || w.__TAURI_INTERNALS__);
}

export function isAndroid(): boolean {
  if (typeof navigator === "undefined") return false;
  return /android/i.test(navigator.userAgent);
}

export function isWeb(): boolean {
  return !isElectron() && !isTauri() && !isAndroid();
}

export function getPlatformName(): PlatformType {
  if (isElectron()) return "electron";
  if (isTauri()) return "tauri";
  if (isAndroid()) return "android";
  return "web";
}
