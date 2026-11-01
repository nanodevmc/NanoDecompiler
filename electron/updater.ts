import { app, ipcMain, shell } from "electron";
import * as https from "https";
import * as fs from "fs";
import * as path from "path";
import * as crypto from "crypto";
import { spawn } from "child_process";
import { GUI_VERSION } from "./version";

// БАГ-ФИКС v1.7.3 (найдено сторонним ревью, согласуется с реальным
// репортом пользователя - краш CLI на Windows с версией/подозрением на
// антивирус): fs.renameSync(src, dest) на Windows использует MoveFileExW
// с MOVEFILE_REPLACE_EXISTING, но замена всё равно может упасть, если
// dest ЗАБЛОКИРОВАН - антивирус ещё сканирует свежескачанный .exe, или
// файл памятью-отображён/открыт другим хендлом. На POSIX rename() поверх
// открытого файла работает всегда (старый inode просто живёт, пока не
// закроют последний хендл) - разницы в поведении раньше не учитывались.
// Явно удаляем старый файл на Windows ПЕРЕД переименованием - если файла
// нет (первая установка) или удаление не удалось (тоже блокировка) -
// ошибка/её отсутствие в обоих случаях всё равно всплывёт на renameSync.
function safeReplaceFile(tmpPath: string, destPath: string): void {
  if (process.platform === "win32") {
    try {
      fs.unlinkSync(destPath);
    } catch {
      /* файла не было - это нормально при первой установке */
    }
  }
  fs.renameSync(tmpPath, destPath);
}

// Подтверждено пользователем: реальный владелец/репозиторий (не заглушка).
const GITHUB_OWNER = "nanodevmc";
const GITHUB_REPO = "NanoDecompiler";

interface GhAsset {
  name: string;
  browser_download_url: string;
}
interface GhRelease {
  tag_name: string;
  html_url: string;
  body?: string;
  assets: GhAsset[];
}
interface VersionsJson {
  client: string;
  api: string;
}
// checksums.json (см. build-and-release.yml) - плоская мапа
// "имя-файла-в-релизе" -> "sha256 hex". Ключи - буквально то же имя, что
// у release-ассета (напр. "NanoDecompilerClApi-windows.exe").
type ChecksumsJson = Record<string, string>;

// SHA256 локального файла - для сверки с checksums.json ДО скачивания
// (не качать заново, если содержимое не изменилось) и ПОСЛЕ (защита от
// оборванного/битого скачивания - см. update:apply ниже).
function sha256File(filePath: string): Promise<string | null> {
  return new Promise((resolve) => {
    try {
      const hash = crypto.createHash("sha256");
      const stream = fs.createReadStream(filePath);
      stream.on("data", (chunk) => hash.update(chunk));
      stream.on("end", () => resolve(hash.digest("hex")));
      stream.on("error", () => resolve(null));
    } catch {
      resolve(null);
    }
  });
}

// БАГ-ФИКС: раньше здесь искался ассет "NanoDecompilerCLI.exe" - имя из
// СТАРОЙ схемы релиза (единственный build-api job, только Windows). После
// объединения CLI+API в "ClApi" и разбивки на 3 платформенных job'а (см.
// .github/workflows/build-and-release.yml) ассеты релиза теперь называются
// NanoDecompilerClApi-windows.exe / -linux / -macos - апдейтер тихо
// переставал находить движок в новых релизах, откатываясь на "обновлять
// нечего". Заодно тихий патч движка на лету (updateKind "engine") теперь
// доступен на всех трёх ОС, а не только Windows - раньше это было
// осознанным ограничением ИЗ-ЗА того, что build-api был Windows-only,
// но эта причина больше не существует.
function findEngineAsset(assets: GhAsset[]): GhAsset | undefined {
  const isWin = process.platform === "win32";
  const isMac = process.platform === "darwin";
  
  if (isWin) {
    return assets.find(a => /^NanoDecompilerCLI-Windows-x64-v.*\.exe$/i.test(a.name)) ||
           assets.find(a => a.name === "NanoDecompilerClApi-windows.exe") ||
           assets.find(a => a.name === "NanoDecompilerCLI.exe");
  } else if (isMac) {
    return assets.find(a => /^NanoDecompilerCLI-macOS-(arm64|x64)-v.*$/i.test(a.name)) ||
           assets.find(a => a.name === "NanoDecompilerClApi-macos") ||
           assets.find(a => a.name === "NanoDecompilerCLI");
  } else {
    return assets.find(a => /^NanoDecompilerCLI-Linux-x64-v.*$/i.test(a.name)) ||
           assets.find(a => a.name === "NanoDecompilerClApi-linux") ||
           assets.find(a => a.name === "NanoDecompilerCLI");
  }
}

// БАГ-ФИКС (реальный, воспроизведён пользователем - "движок.exe называется
// по-другому"): ENGINE_ASSET_NAME - это имя файла В РЕЛИЗЕ на GitHub
// (совпадает с тем, что публикует build-and-release.yml). ЛОКАЛЬНЫЙ файл,
// который реально спавнит main.ts (engineInvocation/binName), называется
// ИНАЧЕ - "NanoDecompilerCLI.exe"/"NanoDecompilerCLI", без "ClApi"/"-win"
// в имени. Раньше update:apply писал скачанный файл под ENGINE_ASSET_NAME
// (т.е. "NanoDecompilerClApi-windows.exe") - это создавало ВТОРОЙ,
// НИКЕМ НЕ ИСПОЛЬЗУЕМЫЙ файл рядом с реальным движком, который
// main.ts продолжал спавнить в СТАРОЙ версии - обновление "успешно"
// скачивалось в никуда, а реальный движок оставался нетронутым (отсюда
// незнающая причина ошибок ПОСЛЕ обновления - пользователь по факту
// продолжал использовать старый бинарник, свежий никогда не подключался).
function localEngineBinaryName(): string {
  return process.platform === "win32" ? "NanoDecompilerCLI.exe" : "NanoDecompilerCLI";
}

// Регэксп имени клиентского инсталлятора - см. package.json build.*.artifactName
// по умолчанию (electron-builder) и .github/workflows/build-and-release.yml
// (там же явно копируется под фиксированное имя перед публикацией).
function clientAssetPattern(): RegExp {
  if (process.platform === "win32") return /Setup\.exe$/i;
  if (process.platform === "darwin") return /\.dmg$/i;
  return /\.AppImage$/i;
}

// См. HANDOFF_16/19 - пользователь просил именно 5 сек для ПРОВЕРКИ
// (лёгкие JSON-запросы к api.github.com) - для СКАЧИВАНИЯ файлов (exe/
// инсталлятор, могут быть десятки МБ) отдельный, более щедрый таймаут -
// 5 сек для реальной закачки было бы слишком мало и рвало бы её на
// медленном интернете.
// БАГ-ФИКС: 5с было слишком жёстко именно для ТИХОЙ автопроверки на
// старте приложения - на холодном старте (первое HTTPS-соединение,
// антивирус/корпоративный прокси делают glubокую инспекцию трафика,
// VPN и т.п.) 5с легко превышаются без какой-либо реальной проблемы с
// сетью - пользователь видел "Таймаут запроса к GitHub" при попытке
// открыть панель обновлений, хотя сам НИКОГДА не запускал проверку
// вручную (silent-режим на старте не показывает toast с ошибкой, но
// сохраняет её в состоянии - см. checkForUpdates в engine.tsx).
const CHECK_TIMEOUT_MS = 12000;
const DOWNLOAD_TIMEOUT_MS = 30000;

// ---------------------------------------------------------------------------
// Сетевые хелперы апдейтера.
//
// БЕЗОПАСНОСТЬ: адрес скачивания приходит из рендерера по IPC, а редиректы
// ведёт сервер. Раньше:
//   - хост не проверялся вообще (любой https-адрес, любые редиректы);
//   - относительный Location не разрешался, петля редиректов была бесконечной;
//   - при обрыве соединения WriteStream не закрывался (утечка дескриптора) и
//     недокачанный `.download` оставался на диске;
//   - размер ответа/файла ничем не ограничивался.
// ---------------------------------------------------------------------------
const MAX_REDIRECTS = 5;
const MAX_JSON_BYTES = 5 * 1024 * 1024;
const MAX_DOWNLOAD_BYTES = 1024 * 1024 * 1024; // 1 ГиБ - с большим запасом для установщика

/**
 * Разрешённые адреса: только https и только GitHub (сам репозиторий проекта
 * на первом шаге; на редиректах - ещё и CDN релизных ассетов *.githubusercontent.com).
 */
function isAllowedUpdateUrl(raw: string, firstHop: boolean): boolean {
  let u: URL;
  try {
    u = new URL(raw);
  } catch {
    return false;
  }
  if (u.protocol !== "https:" || u.username || u.password) return false;
  if (u.port && u.port !== "443") return false;
  const host = u.hostname.toLowerCase();
  const repoPath = `/${GITHUB_OWNER}/${GITHUB_REPO}/`.toLowerCase();
  const apiPath = `/repos/${GITHUB_OWNER}/${GITHUB_REPO}/`.toLowerCase();
  const pathname = u.pathname.toLowerCase();
  if (host === "github.com") return !firstHop || pathname.startsWith(repoPath);
  if (host === "api.github.com") return !firstHop || pathname.startsWith(apiPath);
  return !firstHop && host.endsWith(".githubusercontent.com");
}

function resolveRedirect(from: string, location: string): string | null {
  try {
    return new URL(location, from).toString();
  } catch {
    return null;
  }
}

function httpsGetJson<T>(url: string, redirects = 0): Promise<T> {
  return new Promise((resolve, reject) => {
    if (!isAllowedUpdateUrl(url, redirects === 0)) {
      reject(new Error(`Недопустимый адрес запроса: ${url}`));
      return;
    }
    const req = https.get(url, { headers: { "User-Agent": "NanoDecompiler-updater" } }, (res) => {
      if (res.statusCode && res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        // БАГ-ФИКС v1.7.3: res.resume() ОБЯЗАТЕЛЕН перед тем, как уйти в
        // рекурсию по редиректу - иначе непрочитанный ответ держит сокет
        // занятым (см. комментарий у safeReplaceFile() выше про находки
        // стороннего ревью). Ветка "статус не 200" ниже это уже делала,
        // ветка редиректа - нет, хотя именно она срабатывает чаще всего
        // (GitHub API/релизы почти всегда отвечают редиректом).
        res.resume();
        const next = resolveRedirect(url, res.headers.location);
        if (!next || redirects >= MAX_REDIRECTS) {
          reject(new Error("Слишком много редиректов или некорректный Location"));
          return;
        }
        resolve(httpsGetJson<T>(next, redirects + 1));
        return;
      }
      if (res.statusCode !== 200) {
        if (res.statusCode === 404) {
          reject(new Error("Центр обновлений временно недоступен!"));
        } else {
          reject(new Error(`HTTP ${res.statusCode} при запросе ${url}`));
        }
        res.resume();
        return;
      }
      // setEncoding: `data += chunk` над Buffer'ами ломал многобайтовые
      // UTF-8 символы (кириллица в описании релиза) на границе чанков.
      res.setEncoding("utf8");
      let data = "";
      res.on("data", (chunk: string) => {
        data += chunk;
        if (data.length > MAX_JSON_BYTES) req.destroy(new Error("Ответ сервера слишком большой"));
      });
      res.on("end", () => {
        try {
          resolve(JSON.parse(data));
        } catch (e) {
          reject(e);
        }
      });
      res.on("error", reject);
    });
    req.on("error", reject);
    req.setTimeout(CHECK_TIMEOUT_MS, () => req.destroy(new Error("Таймаут запроса к GitHub")));
  });
}

function httpsDownloadFile(
  url: string,
  destPath: string,
  onProgress?: (downloaded: number, total: number | null) => void,
  redirects = 0,
): Promise<void> {
  return new Promise((resolve, reject) => {
    if (!isAllowedUpdateUrl(url, redirects === 0)) {
      reject(new Error(`Недопустимый адрес загрузки: ${url}`));
      return;
    }
    const tmpPath = destPath + ".download";
    let settled = false;
    let file: fs.WriteStream | null = null;

    // Единая точка отказа: закрывает сокет И файловый дескриптор и удаляет
    // недокачанный файл (раньше при обрыве/таймауте они оставались).
    const fail = (err: Error) => {
      if (settled) return;
      settled = true;
      req.destroy();
      if (file) file.destroy();
      fs.unlink(tmpPath, () => {});
      reject(err);
    };

    const req = https.get(url, { headers: { "User-Agent": "NanoDecompiler-updater" } }, (res) => {
      if (res.statusCode && res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        // БАГ-ФИКС v1.7.3: см. комментарий в httpsGetJson выше - тот же
        // пропущенный res.resume() перед редиректом.
        res.resume();
        const next = resolveRedirect(url, res.headers.location);
        if (!next || redirects >= MAX_REDIRECTS) {
          fail(new Error("Слишком много редиректов или некорректный Location"));
          return;
        }
        settled = true; // этот уровень больше не отвечает за результат
        httpsDownloadFile(next, destPath, onProgress, redirects + 1).then(resolve, reject);
        return;
      }
      if (res.statusCode !== 200) {
        res.resume();
        fail(new Error(`HTTP ${res.statusCode} при скачивании ${url}`));
        return;
      }
      const totalHeader = res.headers["content-length"];
      const total = totalHeader ? parseInt(Array.isArray(totalHeader) ? totalHeader[0] : totalHeader, 10) : null;
      if (total !== null && (!Number.isFinite(total) || total > MAX_DOWNLOAD_BYTES)) {
        res.resume();
        fail(new Error("Размер загрузки превышает допустимый лимит"));
        return;
      }
      let downloaded = 0;
      // Троттлинг - см. HANDOFF_22: если слать прогресс на КАЖДЫЙ chunk
      // (могут прилетать десятки раз в секунду на быстром интернете),
      // получаем ту же болезнь, что и с логом декомпиляции - шквал IPC +
      // рендеров вместо плавного счётчика. Раз в ~120мс более чем
      // достаточно для человеческого глаза (это уже ~8 обновлений/сек).
      let lastEmit = 0;
      if (onProgress) onProgress(0, total);
      file = fs.createWriteStream(tmpPath);
      res.on("data", (chunk: Buffer) => {
        downloaded += chunk.length;
        if (downloaded > MAX_DOWNLOAD_BYTES) {
          fail(new Error("Размер загрузки превышает допустимый лимит"));
          return;
        }
        const now = Date.now();
        if (onProgress && now - lastEmit >= 120) {
          lastEmit = now;
          onProgress(downloaded, total);
        }
      });
      res.on("error", (err) => fail(err));
      // Обрыв соединения: 'end' не приходит, раньше промис просто висел до таймаута.
      res.on("close", () => {
        if (!res.complete) fail(new Error("Соединение оборвано до завершения загрузки"));
      });
      res.pipe(file);
      file.on("finish", () => {
        file!.close((closeErr) => {
          if (settled) return;
          if (closeErr) return fail(closeErr);
          if (total !== null && downloaded !== total) {
            return fail(new Error("Файл скачан не полностью"));
          }
          if (onProgress) onProgress(downloaded, total);
          // Атомарная замена - переименование внутри одной файловой системы
          // почти мгновенное, не оставляет "битого" файла на середине,
          // если что-то пойдёт не так ДО этого момента. safeReplaceFile()
          // (см. выше) добавляет предварительный unlink на Windows -
          // см. БАГ-ФИКС v1.7.3 там же.
          try {
            safeReplaceFile(tmpPath, destPath);
          } catch (e) {
            return fail(e as Error);
          }
          settled = true;
          resolve();
        });
      });
      file.on("error", (err) => fail(err));
    });
    req.on("error", (err) => fail(err));
    req.setTimeout(DOWNLOAD_TIMEOUT_MS, () => fail(new Error("Таймаут скачивания")));
  });
}

/**
 * checksums.json релиза (плоская мапа "имя ассета" -> sha256 hex). Возвращает
 * только корректные записи (64 hex-символа, нижний регистр) либо null, если
 * файла нет/он не читается - вызывающая сторона обязана ОТКАЗАТЬ в установке.
 */
async function fetchReleaseChecksums(release: GhRelease): Promise<ChecksumsJson | null> {
  const asset = release.assets.find((a) => a.name === "checksums.json");
  if (!asset) return null;
  try {
    const parsed = await httpsGetJson<unknown>(asset.browser_download_url);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) return null;
    const out: ChecksumsJson = {};
    for (const [name, value] of Object.entries(parsed as Record<string, unknown>)) {
      if (typeof value === "string" && /^[0-9a-fA-F]{64}$/.test(value)) out[name] = value.toLowerCase();
    }
    return out;
  } catch {
    return null;
  }
}

// См. HANDOFF_7/8/16 - версия ДВИЖКА (api) хранится в маленьком текстовом
// файле рядом с exe (обновляется после каждого успешного apply), а не в
// константе кода - см. предыдущие аддендумы про то, почему сравнение тегов
// "в лоб" было сломано. Версия КЛИЕНТА никакого отдельного файла не
// требует - её всегда точно знает сам Electron через app.getVersion()
// (читает package.json "version", который electron-builder зашивает в
// собранный .exe/инсталлятор на этапе сборки) - именно она "живёт" в
// файловой системе как факт того, какой инсталлятор реально стоит.
function apiVersionMarkerPath(engineDir: string): string {
  return path.join(engineDir, ".engine-api-version");
}

function writeInstalledApiVersion(engineDir: string, version: string): void {
  fs.writeFileSync(apiVersionMarkerPath(engineDir), version, "utf-8");
}

// БАГ-ФИКС: раньше версия движка бралась ТОЛЬКО из файла-маркера
// (.engine-api-version), который писался ИСКЛЮЧИТЕЛЬНО после успешного
// update:apply - на свежей установке (маркер никогда не создавался)
// installedApiVersion был всегда null -> apiNeedsUpdate всегда true ->
// апдейтер ВЕЧНО показывал "доступно обновление движка", даже когда
// встроенный в свежий инсталлятор движок уже последней версии. Теперь
// реально спрашиваем сам бинарник через --version (см. cli_main.cpp) - и
// только на случай, если сам бинарник почему-то не смог ответить (не
// найден и т.п.), откатываемся на маркер-файл как на кэш последнего
// известного значения.
function extractVersionNumber(raw: string): string | null {
  // Извлекает версию, сохраняя суффиксы вроде -ObfUpd.16 или -beta.1
  const m = raw.match(/v?(\d+(?:\.\d+)+(?:-[A-Za-z0-9_.]*)?)/);
  return m ? m[1] : null;
}

function resolveInstalledApiVersion(
  engineDir: string,
  engineInvocation: (scriptArgs: string[]) => { cmd: string; args: string[] },
): Promise<string | null> {
  return new Promise((resolve) => {
    let cmd: string, args: string[];
    try {
      ({ cmd, args } = engineInvocation(["--version"]));
    } catch {
      resolve(readMarkerFallback(engineDir));
      return;
    }
    let out = "";
    let settled = false;
    const finish = (v: string | null) => {
      if (settled) return;
      settled = true;
      resolve(v ?? readMarkerFallback(engineDir));
    };
    try {
      const proc = spawn(cmd, args, { env: { ...process.env } });
      // Зависший/подменённый движок не должен вешать проверку обновлений навсегда.
      const killTimer = setTimeout(() => {
        proc.kill();
        finish(null);
      }, 15000);
      proc.stdout.on("data", (d) => (out += d.toString("utf-8")));
      proc.on("close", () => {
        clearTimeout(killTimer);
        try {
          const parsed = JSON.parse(out.trim());
          const num = parsed.version ? extractVersionNumber(String(parsed.version)) : null;
          if (num) writeInstalledApiVersion(engineDir, num);
          finish(num);
        } catch {
          finish(null);
        }
      });
      proc.on("error", () => {
        clearTimeout(killTimer);
        finish(null);
      });
    } catch {
      finish(null);
    }
  });
}

function readMarkerFallback(engineDir: string): string | null {
  try {
    return fs.readFileSync(apiVersionMarkerPath(engineDir), "utf-8").trim() || null;
  } catch {
    return null;
  }
}

// "v1.2" / "1.2" / "1.2.0" - сравниваем по числовым компонентам, а не строкой 1:1.
function versionParts(v: string): number[] {
  return v
    .replace(/^v/i, "")
    .split(".")
    .map((p) => parseInt(p, 10) || 0);
}

// БАГ-ФИКС: корректное сравнение версий с поддержкой суффиксов (-ObfUpd.16).
// Равенство строк (без префикса 'v') ВСЕГДА даёт 0, предотвращая показ vX -> vX.
function compareVersions(a: string, b: string): -1 | 0 | 1 {
  const normA = a.trim().replace(/^v/i, "");
  const normB = b.trim().replace(/^v/i, "");
  if (normA === normB) return 0;

  const [baseA, tagA] = normA.split("-");
  const [baseB, tagB] = normB.split("-");

  const pa = versionParts(baseA);
  const pb = versionParts(baseB);
  const len = Math.max(pa.length, pb.length);
  for (let i = 0; i < len; i++) {
    const va = pa[i] ?? 0;
    const vb = pb[i] ?? 0;
    if (va !== vb) return va > vb ? 1 : -1;
  }

  // При одинаковых базовых версиях сравниваем суффиксы
  if (!tagA && tagB) return -1; // У релиза есть патч/суффикс, а у локальной нет -> релиз новее
  if (tagA && !tagB) return 0;  // Локальная версия имеет патч той же базы (ReadUpd/ObfUpd) -> актуальна
  if (tagA && tagB) {
    if (tagA === tagB) return 0;
    const numA = parseInt(tagA.replace(/^[^\d]*/, ""), 10);
    const numB = parseInt(tagB.replace(/^[^\d]*/, ""), 10);
    if (!isNaN(numA) && !isNaN(numB) && numA !== numB) {
      return numA > numB ? 1 : -1;
    }
    return tagA.localeCompare(tagB) > 0 ? 1 : -1;
  }

  return 0;
}

// Соглашение о номерах версий проекта (см. HANDOFF_22): релиз - РОВНО два
// числа через точку (1.4, 1.5, 1.6...), без хвостовых нулей на конце.
// Пререлиз/бета - три и больше чисел (1.4.1, 1.4.2, ...) - это
// промежуточные тестовые сборки на пути к следующему релизу. Используется
// при проверке обновлений, чтобы явно сказать пользователю "это бета,
// осторожнее" или "это релиз, можно спокойно пользоваться".
function classifyVersion(v: string): "release" | "prerelease" {
  return versionParts(v).length >= 3 ? "prerelease" : "release";
}

// См. HANDOFF_16 - после "важного" (client) обновления новый инсталлятор
// запускается ОТДЕЛЬНЫМ процессом (detached, переживает закрытие текущего
// приложения), а ТЕКУЩИЙ клиент сам закрывается - Windows не даёт
// перезаписать свой же запущенный .exe. NSIS-инсталлятор electron-builder
// по умолчанию сам предлагает "запустить приложение" по завершении
// (чекбокс включён по умолчанию) - но на случай, если пользователь его
// снимет или что-то пойдёт не так, любой следующий запуск приложения
// (хоть вручную, хоть через сам инсталлятор) проверяет этот файл-маркер
// и показывает плашку "Обновление успешно установлено" - маркер надёжнее
// пробрасывания CLI-флага через инсталлятор (который его сам не передаёт).
function updateSuccessMarkerPath(): string {
  return path.join(app.getPath("userData"), ".pending-update-success");
}

export function writeUpdateSuccessMarker(): void {
  try {
    fs.writeFileSync(updateSuccessMarkerPath(), "1", "utf-8");
  } catch {
    // не критично - в худшем случае просто не покажем плашку один раз
  }
}

// Тот же результат достижим и явным флагом `--add-update-success` (см.
// запрос пользователя) - на случай ручного/скриптового вызова, не только
// через маркер-файл.
export function consumeUpdateSuccessFlag(): boolean {
  const viaFlag = process.argv.includes("--add-update-success");
  let viaMarker = false;
  try {
    viaMarker = fs.existsSync(updateSuccessMarkerPath());
    if (viaMarker) fs.unlinkSync(updateSuccessMarkerPath());
  } catch {
    // игнорируем - не критично
  }
  return viaFlag || viaMarker;
}

/**
 * Установщики, скачанные прошлыми обновлениями, остаются в %TEMP% (запущенный
 * установщик нельзя удалить из самого себя). Подчищаем старше суток на старте.
 */
export function sweepStaleUpdateDirs(maxAgeMs: number = 24 * 60 * 60 * 1000): void {
  try {
    const base = app.getPath("temp");
    const now = Date.now();
    for (const name of fs.readdirSync(base)) {
      if (!name.startsWith("nanodecompiler-update-")) continue;
      const full = path.join(base, name);
      try {
        const st = fs.lstatSync(full);
        if (st.isDirectory() && !st.isSymbolicLink() && now - st.mtimeMs > maxAgeMs) {
          fs.rmSync(full, { recursive: true, force: true });
        }
      } catch {
        // занято/нет прав - в следующий раз
      }
    }
  } catch {
    // нет доступа к temp - не критично
  }
}

export function registerUpdateHandlers(
  engineDir: () => string,
  engineInvocation: (scriptArgs: string[]) => { cmd: string; args: string[] },
) {
  ipcMain.handle("update:consumeSuccessFlag", async () => consumeUpdateSuccessFlag());

  ipcMain.handle("update:installClientAndRestart", async (event, downloadUrl: string) => {
    let workDir = "";
    try {
      if (typeof downloadUrl !== "string" || !downloadUrl) {
        return { ok: false, manual: true, error: "Не указан адрес установщика" };
      }

      // БЕЗОПАСНОСТЬ: адресу из рендерера не доверяем (раньше он скачивался и
      // ЗАПУСКАЛСЯ как установщик без какой-либо проверки хоста и хеша). Адрес
      // обязан совпасть с browser_download_url ассета АКТУАЛЬНОГО релиза нашего
      // репозитория, а скачанный файл - с SHA256 из checksums.json. Иначе
      // установку не выполняем, а отдаём пользователю ручную загрузку.
      const release = await httpsGetJson<GhRelease>(
        `https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases/latest`,
      );
      const asset = release.assets.find((a) => a.browser_download_url === downloadUrl && clientAssetPattern().test(a.name));
      if (!asset) {
        return { ok: false, manual: true, error: "Адрес не соответствует установщику последнего релиза - скачайте обновление вручную" };
      }
      const checksums = await fetchReleaseChecksums(release);
      const expected = checksums?.[asset.name];
      if (!expected) {
        return { ok: false, manual: true, error: "В релизе нет контрольной суммы SHA256 для установщика - автоустановка отменена, скачайте вручную" };
      }

      // Приватный каталог со случайным именем (mkdtemp создаёт его с правами
      // 0700) вместо предсказуемых путей прямо в %TEMP% (NewVerSetup.bat и т.п.).
      workDir = fs.mkdtempSync(path.join(app.getPath("temp"), "nanodecompiler-update-"));
      let downloadPath = "";
      if (process.platform === "win32") downloadPath = path.join(workDir, "NanoDecompiler-Client-Setup.exe");
      else if (process.platform === "darwin") downloadPath = path.join(workDir, "NanoDecompiler-Client.dmg");
      else downloadPath = path.join(workDir, "NanoDecompiler-Client.AppImage");

      await httpsDownloadFile(asset.browser_download_url, downloadPath, (downloaded, total) => {
        event.sender.send("update:downloadProgress", { downloaded, total, kind: "client" });
      });

      const actual = await sha256File(downloadPath);
      if (!actual || actual !== expected) {
        fs.rmSync(workDir, { recursive: true, force: true });
        return { ok: false, error: "SHA256 установщика не совпадает с checksums.json - установка отменена" };
      }
      writeUpdateSuccessMarker();

      // Что запустить ПОСЛЕ выхода приложения. Раньше это был .bat/.sh, в который
      // пути подставлялись текстом (символы % ^ & " в пути ломали или исполняли
      // команду). Теперь Windows/Linux запускают файл напрямую, macOS - /bin/sh с
      // путями отдельными аргументами ($1, $2, $3), а не частью текста скрипта.
      let launchAfterQuit: () => void;
      if (process.platform === "win32") {
        launchAfterQuit = () => {
          spawn(downloadPath, ["/S", "--force-run"], { detached: true, stdio: "ignore" }).unref();
        };
      } else if (process.platform === "darwin") {
        const appPath = app.getPath("exe").split(".app/")[0] + ".app";
        const mountPoint = fs.mkdtempSync(path.join(workDir, "mnt-"));
        const script = [
          "sleep 2",
          'hdiutil attach "$1" -mountpoint "$3" -nobrowse -readonly -quiet || exit 1',
          'ditto "$3/NanoDecompiler.app" "$2"',
          'hdiutil detach "$3" -force -quiet',
          'open "$2"',
        ].join("\n");
        launchAfterQuit = () => {
          spawn("/bin/sh", ["-c", script, "sh", downloadPath, appPath, mountPoint], { detached: true, stdio: "ignore" }).unref();
        };
      } else {
        const currentAppImage = process.env.APPIMAGE;
        if (!currentAppImage) {
          throw new Error("Не удалось определить путь к текущему AppImage (переменная APPIMAGE пуста)");
        }
        // Подмена делается ЗАРАНЕЕ (на Linux rename поверх запущенного файла
        // безопасен: старый inode живёт, пока его держат): копия рядом (workDir
        // может быть на другой ФС - EXDEV) -> chmod -> атомарный rename.
        const staged = currentAppImage + ".update";
        fs.copyFileSync(downloadPath, staged);
        fs.chmodSync(staged, 0o755);
        fs.renameSync(staged, currentAppImage);
        launchAfterQuit = () => {
          spawn(currentAppImage, [], { detached: true, stdio: "ignore" }).unref();
        };
      }

      app.once("quit", () => {
        try {
          launchAfterQuit();
        } catch {
          // не удалось запустить - пользователь запустит приложение вручную
        }
      });
      setTimeout(() => app.quit(), 700);
      return { ok: true };
    } catch (e) {
      if (workDir) {
        try {
          fs.rmSync(workDir, { recursive: true, force: true });
        } catch {
          // ignore
        }
      }
      return { ok: false, error: String(e) };
    }
  });

  ipcMain.handle("update:check", async () => {
    try {
      const release = await httpsGetJson<GhRelease>(
        `https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases/latest`
      );
      const versionsAsset = release.assets.find((a) => a.name === "versions.json");
      const checksumsAsset = release.assets.find((a) => a.name === "checksums.json");
      const cliAsset = findEngineAsset(release.assets);
      const setupAsset = release.assets.find((a) => clientAssetPattern().test(a.name));

      const currentClientVersion = GUI_VERSION;
      const installedApiVersion = await resolveInstalledApiVersion(engineDir(), engineInvocation);

      // По просьбе пользователя: сверяем SHA256 движка с checksums.json
      // релиза ДО того, как вообще предлагать обновление - если бинарник
      // на диске побайтово совпадает с тем, что в релизе (даже если
      // версионная строка почему-то разошлась - редкий, но возможный
      // случай), апдейт движка не предлагаем вообще, нечего качать.
      let engineHashMatches = false;
      if (checksumsAsset && cliAsset) {
        try {
          const checksums = await fetchReleaseChecksums(release);
          const expectedHash = checksums?.[cliAsset.name];
          const localBin = path.join(engineDir(), localEngineBinaryName());
          const localHash = expectedHash ? await sha256File(localBin) : null;
          engineHashMatches = !!expectedHash && !!localHash && expectedHash === localHash;
        } catch {
          // checksums.json недоступен/битый - не блокируем обновление
          // из-за этого, просто не получаем доп. проверку (при САМОМ
          // применении обновления контрольная сумма уже обязательна).
        }
      }

      if (!versionsAsset) {
        // Старый релиз без versions.json (см. HANDOFF_16) - не можем
        // различить тип обновления, откатываемся на грубое "обычное"
        // поведение по тегу целиком, лишь бы не соврать про "всё ок".
        const tagNorm = release.tag_name.replace(/^v/i, "");
        const curNorm = (installedApiVersion ?? currentClientVersion).replace(/^v/i, "");
        const isSame = tagNorm === curNorm;
        return {
          ok: true,
          updateKind: isSame ? "none" : "engine",
          currentVersion: currentClientVersion,
          latestVersion: release.tag_name,
          latestVersionKind: classifyVersion(release.tag_name),
          downloadUrl: cliAsset ? cliAsset.browser_download_url : null,
          clientDownloadUrl: null,
          releaseUrl: release.html_url,
          changelog: release.body || "",
        };
      }

      const versions = await httpsGetJson<VersionsJson>(versionsAsset.browser_download_url);
      const clientCmp = compareVersions(currentClientVersion, versions.client);
      let clientNeedsUpdate = clientCmp < 0;  // локальная версия СТАРШЕ релиза - реально нужно обновление
      let clientIsAhead = clientCmp > 0;  // локальная версия НОВЕЕ релиза - закрытая бета, не "последняя версия"
      const apiCmp = installedApiVersion !== null ? compareVersions(installedApiVersion, versions.api) : -1;
      let apiNeedsUpdate = installedApiVersion === null || apiCmp < 0;
      let apiIsAhead = installedApiVersion !== null && apiCmp > 0;

      // Строгое равенство нормализованных строк — исключает ложные срабатывания vX -> vX
      const normClient = currentClientVersion.trim().replace(/^v/i, "");
      const normRemoteClient = String(versions.client || "").trim().replace(/^v/i, "");
      if (normClient === normRemoteClient) {
        clientNeedsUpdate = false;
        clientIsAhead = false;
      }

      if (installedApiVersion) {
        const normApi = installedApiVersion.trim().replace(/^v/i, "");
        const normRemoteApi = String(versions.api || "").trim().replace(/^v/i, "");
        if (normApi === normRemoteApi) {
          apiNeedsUpdate = false;
          apiIsAhead = false;
        }
      }

      // Клиент важнее движка: если поменялось само GUI-приложение, апдейт
      // движка (даже если он ТОЖЕ поменялся) неважен сам по себе - новый
      // инсталлятор клиента и так принесёт свежий движок внутри себя.
      let updateKind: "none" | "engine" | "client" | "closed_beta" = "none";
      if (clientNeedsUpdate) updateKind = "client";
      else if (clientIsAhead) updateKind = "closed_beta";
      else if (apiNeedsUpdate && cliAsset && !engineHashMatches) updateKind = "engine";
      else if (apiIsAhead) updateKind = "closed_beta";

      return {
        ok: true,
        updateKind,
        currentVersion: currentClientVersion,
        latestVersion: updateKind === "client" ? versions.client : updateKind === "closed_beta" ? versions.client : versions.api,
        latestVersionKind: classifyVersion(updateKind === "client" ? versions.client : versions.api),
        downloadUrl: cliAsset ? cliAsset.browser_download_url : null,
        clientDownloadUrl: setupAsset ? setupAsset.browser_download_url : release.html_url,
        releaseUrl: release.html_url,
        changelog: release.body || "",
      };
    } catch (e: any) {
      const errStr = String(e?.message || e);
      if (errStr.includes("404") || errStr.includes("Центр обновлений временно недоступен")) {
        return { ok: false, error: "Центр обновлений временно недоступен!" };
      }
      return { ok: false, error: errStr };
    }
  });

  ipcMain.handle("update:apply", async (event, _downloadUrl: string, latestApiVersion?: string) => {
    // ВАЖНО: apply умеет подменять ТОЛЬКО движок (exe) - см. updateKind
    // выше. Обновление самого клиента "апдейтом" в этом же смысле в
    // принципе невозможно - Windows не даёт процессу перезаписать
    // собственный запущенный .exe/удалить свою же папку в Program Files;
    // для клиента showUpdateKind "client" в App.tsx ведёт на скачивание
    // НОВОГО инсталлятора через shell.openExternal, а не сюда.
    //
    // БЕЗОПАСНОСТЬ: адрес из рендерера (_downloadUrl) больше не используется -
    // движок берётся из ассета актуального релиза нашего репозитория, а
    // отсутствие/несовпадение SHA256 ТЕПЕРЬ ОТМЕНЯЕТ обновление (раньше при
    // недоступном checksums.json бинарник применялся непроверенным, и
    // исполняемый файл подменялся чем угодно, что отдал сервер).
    let tmpDest = "";
    try {
      const dir = engineDir();
      const dest = path.join(dir, localEngineBinaryName());

      const release = await httpsGetJson<GhRelease>(
        `https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases/latest`,
      );
      const asset = findEngineAsset(release.assets);
      if (!asset) return { ok: false, error: "в последнем релизе нет бинарника движка для этой платформы" };
      const checksums = await fetchReleaseChecksums(release);
      const expected = checksums?.[asset.name];
      if (!expected) {
        return { ok: false, error: "не удалось получить контрольную сумму SHA256 движка - обновление отменено" };
      }

      // БАГ-ФИКС/фича: скачиваем во ВРЕМЕННЫЙ файл, а не сразу поверх
      // рабочего движка - если скачивание оборвётся/повредится (см.
      // "движок завершился с кодом N без вывода - возможно, файл повреждён
      // при скачивании" в main.ts), рабочая копия остаётся нетронутой.
      // httpsDownloadFile сам кладёт данные в `<dest>.download` и атомарно
      // переименовывает в указанный путь - поэтому качаем в отдельное имя.
      tmpDest = dest + ".verify";
      await httpsDownloadFile(asset.browser_download_url, tmpDest, (downloaded, total) => {
        event.sender.send("update:downloadProgress", { downloaded, total, kind: "engine" });
      });

      const actual = await sha256File(tmpDest);
      if (!actual || actual !== expected) {
        fs.unlinkSync(tmpDest);
        return { ok: false, error: "скачанный файл повреждён (SHA256 не совпадает) - попробуйте ещё раз" };
      }

      safeReplaceFile(tmpDest, dest);
      tmpDest = "";
      if (process.platform !== "win32") {
        try {
          fs.chmodSync(dest, 0o755);
        } catch {
          // не критично - если бинарник уже был +x, rename сохраняет права
        }
      }
      const safeVersion = typeof latestApiVersion === "string" && /^[0-9A-Za-z._+-]{1,64}$/.test(latestApiVersion) ? latestApiVersion : null;
      if (safeVersion) {
        writeInstalledApiVersion(dir, safeVersion);
      }

      // Синхронизация с установленной версией в AppData/Local/Programs
      if (process.platform === "win32") {
        try {
          const appNames = ["NanoDecompiler", "nano-decompiler-gui"];
          for (const appName of appNames) {
            const installedDir = path.join(process.env.LOCALAPPDATA || "", "Programs", appName, "resources", "engine");
            if (fs.existsSync(installedDir) && installedDir.toLowerCase() !== dir.toLowerCase()) {
              const installedDest = path.join(installedDir, localEngineBinaryName());
              fs.copyFileSync(dest, installedDest);
              if (safeVersion) writeInstalledApiVersion(installedDir, safeVersion);
            }
          }
        } catch {
          // игнорируем ошибку если нет прав или программа не установлена
        }
      }

      try {
        // eslint-disable-next-line @typescript-eslint/no-var-requires
        const { resetEngineVersionCache } = require("./main");
        if (typeof resetEngineVersionCache === "function") {
          resetEngineVersionCache();
        }
      } catch {
        // fallback
      }

      return { ok: true };
    } catch (e) {
      if (tmpDest) {
        try {
          fs.unlinkSync(tmpDest);
        } catch {
          // ignore
        }
      }
      return { ok: false, error: String(e) };
    }
  });
}
