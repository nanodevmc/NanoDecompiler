import { app, ipcMain } from "electron";
import * as path from "path";
import * as fs from "fs";
import * as crypto from "crypto";
import * as https from "https";
import * as http from "http";

export interface OtaModuleInfo {
  name: string;
  version: string;
  sha256: string;
  sizeBytes: number;
  url: string;
}

export interface OtaManifest {
  manifestVersion: string;
  releaseTag: string;
  generatedAt?: string;
  channels?: string[];
  components?: Record<string, any>;
  otaPackages?: Record<string, OtaModuleInfo>;
  guiModules?: Record<string, OtaModuleInfo>;
  engineModules?: Record<string, OtaModuleInfo>;
}

function httpsGetWithRedirects(
  targetUrl: string,
  headers: Record<string, string>,
  callback: (res: http.IncomingMessage) => void,
  maxRedirects = 5
): void {
  try {
    const parsed = new URL(targetUrl);
    const client = parsed.protocol === "http:" ? http : https;
    client.get(targetUrl, { headers }, (res) => {
      const code = res.statusCode || 0;
      if ([301, 302, 303, 307, 308].includes(code) && res.headers.location && maxRedirects > 0) {
        const nextUrl = new URL(res.headers.location, targetUrl).toString();
        return httpsGetWithRedirects(nextUrl, headers, callback, maxRedirects - 1);
      }
      callback(res);
    }).on("error", () => {
      callback({ statusCode: 500 } as any);
    });
  } catch {
    callback({ statusCode: 500 } as any);
  }
}

export class OtaManager {
  private modulesDir: string;

  constructor() {
    this.modulesDir = path.join(app.getPath("userData"), "ota-modules");
    this.ensureDir();
  }

  private ensureDir(): void {
    try {
      if (!fs.existsSync(this.modulesDir)) {
        fs.mkdirSync(this.modulesDir, { recursive: true, mode: 0o700 });
      }
    } catch {
      // Игнорируем ошибки доступа
    }
  }

  public getModulesDir(): string {
    return this.modulesDir;
  }

  public async sha256File(filePath: string): Promise<string | null> {
    return new Promise((resolve) => {
      if (!fs.existsSync(filePath)) return resolve(null);
      const hash = crypto.createHash("sha256");
      const stream = fs.createReadStream(filePath);
      stream.on("data", (chunk) => hash.update(chunk));
      stream.on("end", () => resolve(hash.digest("hex")));
      stream.on("error", () => resolve(null));
    });
  }

  public async fetchManifest(manifestUrl: string): Promise<OtaManifest | null> {
    return new Promise((resolve) => {
      try {
        const u = new URL(manifestUrl);
        if (u.protocol !== "https:" || (!u.hostname.endsWith("github.com") && !u.hostname.endsWith("githubusercontent.com"))) {
          return resolve(null);
        }
        httpsGetWithRedirects(manifestUrl, { "User-Agent": "NanoDecompiler-OTA" }, (res) => {
          if (res.statusCode !== 200) return resolve(null);
          let data = "";
          res.on("data", (c) => (data += c.toString("utf-8")));
          res.on("end", () => {
            try {
              const manifest = JSON.parse(data) as OtaManifest;
              if (manifest.otaPackages && !manifest.guiModules) {
                manifest.guiModules = manifest.otaPackages;
              }
              resolve(manifest);
            } catch {
              resolve(null);
            }
          });
          res.on("error", () => resolve(null));
        });
      } catch {
        resolve(null);
      }
    });
  }

  public async syncModule(mod: OtaModuleInfo, onProgress?: (pct: number) => void): Promise<{ ok: boolean; path?: string; error?: string }> {
    this.ensureDir();
    const dest = path.join(this.modulesDir, mod.name);
    const tmp = dest + ".otatmp";

    // Проверяем существующий
    const localHash = await this.sha256File(dest);
    if (localHash && mod.sha256 && localHash.toLowerCase() === mod.sha256.toLowerCase()) {
      return { ok: true, path: dest };
    }

    return new Promise((resolve) => {
      try {
        const out = fs.createWriteStream(tmp);
        httpsGetWithRedirects(mod.url, { "User-Agent": "NanoDecompiler-OTA" }, (res) => {
          if (res.statusCode !== 200) {
            out.close();
            try { fs.unlinkSync(tmp); } catch {}
            return resolve({ ok: false, error: `HTTP ${res.statusCode}` });
          }
          let downloaded = 0;
          const total = mod.sizeBytes || parseInt(res.headers["content-length"] || "0", 10);
          res.on("data", (chunk) => {
            downloaded += chunk.length;
            if (total > 0 && onProgress) onProgress(Math.min(100, Math.round((downloaded / total) * 100)));
          });
          res.pipe(out);
          out.on("finish", async () => {
            out.close();
            const actualHash = await this.sha256File(tmp);
            if (mod.sha256 && mod.sha256 !== "auto") {
              if (!actualHash || actualHash.toLowerCase() !== mod.sha256.toLowerCase()) {
                try { fs.unlinkSync(tmp); } catch {}
                return resolve({ ok: false, error: "SHA-256 mismatch for OTA module" });
              }
            }
            try {
              if (fs.existsSync(dest)) fs.unlinkSync(dest);
              fs.renameSync(tmp, dest);
              resolve({ ok: true, path: dest });
            } catch (e) {
              resolve({ ok: false, error: String(e) });
            }
          });
          out.on("error", (e) => {
            try { fs.unlinkSync(tmp); } catch {}
            resolve({ ok: false, error: String(e) });
          });
        });
      } catch (e) {
        resolve({ ok: false, error: String(e) });
      }
    });
  }

  public registerIpc(): void {
    ipcMain.handle("ota:listLocal", async () => {
      try {
        if (!fs.existsSync(this.modulesDir)) return [];
        return fs.readdirSync(this.modulesDir).filter(f => !f.endsWith(".otatmp"));
      } catch {
        return [];
      }
    });

    ipcMain.handle("ota:fetchManifest", async (_e, manifestUrl?: string) => {
      const url = manifestUrl || "https://raw.githubusercontent.com/nanodevmc/NanoDecompiler/main/ota-manifest.json";
      return await this.fetchManifest(url);
    });

    ipcMain.handle("ota:syncModule", async (_e, mod: OtaModuleInfo) => {
      if (!mod || !mod.name || !mod.url) return { ok: false, error: "Некорректный дескриптор модуля" };
      return await this.syncModule(mod);
    });
  }
}
