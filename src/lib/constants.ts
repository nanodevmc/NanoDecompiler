/**
 * constants.ts — Единые константы и эндпоинты NanoDecompiler
 * Предотвращает дублирование URL манифестов, версий и API-путей (DRY).
 */

export const DEFAULT_APP_VERSION = "2.0.8";

export const GITHUB_REPO_OWNER = "nanodevmc";
export const GITHUB_REPO_NAME = "NanoDecompiler";

export const GITHUB_RELEASES_PAGE_URL = `https://github.com/${GITHUB_REPO_OWNER}/${GITHUB_REPO_NAME}/releases`;
export const CANONICAL_RELEASE_API_URL = `https://api.github.com/repos/${GITHUB_REPO_OWNER}/${GITHUB_REPO_NAME}/releases/latest`;

export const MANIFEST_CANDIDATE_URLS: readonly string[] = [
  `https://raw.githubusercontent.com/${GITHUB_REPO_OWNER}/${GITHUB_REPO_NAME}/main/ota-manifest.json`,
  `https://github.com/${GITHUB_REPO_OWNER}/${GITHUB_REPO_NAME}/releases/latest/download/ota-manifest.json`,
] as const;

export const TELEMETRY_ENDPOINT = "https://telemetry.nanodev.fun/api/report";
