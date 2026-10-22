import * as fs from "fs";
import * as path from "path";
import * as os from "os";
import { execFile, spawn } from "child_process";
import { promisify } from "util";
import { app } from "electron";
import * as tar from "tar";
import { readJarSummaryNative, type JarSummary } from "./jarSummary";
import { listZipEntries } from "./zipReader";

const execFileAsync = promisify(execFile);

// eslint-disable-next-line @typescript-eslint/no-var-requires
const unrar = require("node-unrar-js");
// eslint-disable-next-line @typescript-eslint/no-var-requires
const sevenBin = require("7zip-bin");

// Лимиты на распаковку НЕДОВЕРЕННЫХ архивов (zip/tar-bomb): без них архив в
// несколько килобайт мог развернуться в сотни гигабайт и забить диск.
const MAX_EXTRACT_TOTAL_BYTES = 4 * 1024 * 1024 * 1024; // 8 ГиБ суммарно
const MAX_EXTRACT_ENTRIES = 200000;
// Старые временные папки распаковки (после падения приложения/закрытия без
// очистки) подчищаем при старте - иначе они копились бесконечно.
const STALE_TEMP_MAX_AGE_MS = 24 * 60 * 60 * 1000;

/** Корень временных папок распаковки (тот же путь использует C++ движок). */
export function archivesRootDir(): string {
  return path.join(os.tmpdir(), "NanoDecompiler", "archives");
}

/** Удаляет папки распаковки старше STALE_TEMP_MAX_AGE_MS. Безопасно вызывать на старте. */
export async function sweepStaleArchiveTemps(maxAgeMs: number = STALE_TEMP_MAX_AGE_MS): Promise<void> {
  const root = archivesRootDir();
  let names: string[];
  try {
    names = await fs.promises.readdir(root);
  } catch {
    return;
  }
  const now = Date.now();
  for (const name of names) {
    const full = path.join(root, name);
    try {
      const st = await fs.promises.lstat(full);
      if (st.isSymbolicLink()) {
        await fs.promises.unlink(full);
        continue;
      }
      if (st.isDirectory() && now - st.mtimeMs > maxAgeMs) {
        await fs.promises.rm(full, { recursive: true, force: true });
      }
    } catch {
      // занято/нет прав - попробуем при следующем запуске
    }
  }
}

/** Суммарный размер файлов в каталоге; прекращает обход, как только превышен limit. */
async function dirSizeExceeds(dir: string, limit: number): Promise<boolean> {
  let total = 0;
  const stack = [dir];
  while (stack.length) {
    const cur = stack.pop()!;
    let entries: fs.Dirent[];
    try {
      entries = await fs.promises.readdir(cur, { withFileTypes: true });
    } catch {
      continue;
    }
    for (const e of entries) {
      const full = path.join(cur, e.name);
      if (e.isDirectory()) {
        stack.push(full);
      } else if (e.isFile()) {
        try {
          total += (await fs.promises.stat(full)).size;
        } catch {
          // файл исчез - пропускаем
        }
        if (total > limit) return true;
      }
    }
  }
  return false;
}

export interface ArchiveProgressEvent {
  archivePath: string;
  percent: number;
  currentFile: string;
  extractedFiles: number;
  totalFiles: number;
  etaSeconds: number;
  speedBytesPerSec: number;
  tempDir: string;
}

export interface DiscoveredPlugin {
  fileName: string;
  jarPath: string;
  relPath: string;
  sizeBytes: number;
  classCount: number | null;
  pluginName: string | null;
  pluginAuthor: string | null;
  platform: string;
  isPlugin: boolean;
  isServerCore: boolean;
  coreReason?: string;
}

export interface ArchiveExtractResult {
  ok: boolean;
  archivePath: string;
  tempDir: string;
  plugins: DiscoveredPlugin[];
  serverCores: DiscoveredPlugin[];
  skippedNonPlugins: number;
  totalJarsFound: number;
  archiveSizeBytes?: number;
  error?: string;
}

/** Проверяет, является ли файл поддерживаемым архивом */
export function isSupportedArchive(filePath: string): boolean {
  const lower = filePath.toLowerCase();
  return (
    lower.endsWith(".zip") ||
    lower.endsWith(".tar.gz") ||
    lower.endsWith(".tgz") ||
    lower.endsWith(".tar") ||
    lower.endsWith(".7z") ||
    lower.endsWith(".7zip") ||
    lower.endsWith(".rar")
  );
}

/**
 * Каталог для кэша распакованного из asar 7za. Раньше это был
 * `os.tmpdir()/NanoDecompiler/bin` - общий предсказуемый путь (в /tmp на
 * Linux/macOS его может заранее создать и подложить свой "7za" любой
 * локальный пользователь/процесс, и мы бы его запустили). Теперь - каталог
 * данных самого приложения, доступный только текущему пользователю.
 */
function userBinDir(): string {
  return path.join(app.getPath("userData"), "bin");
}

/** Получает путь к бинарнику 7za, с поддержкой asar.unpacked и авто-извлечения */
function get7zaPath(): string | null {
  const binName = process.platform === "win32" ? "7za.exe" : "7za";

  try {
    if (sevenBin && sevenBin.path7za) {
      let p: string = sevenBin.path7za;
      if (p.includes("app.asar")) {
        const unpacked = p.replace("app.asar", "app.asar.unpacked");
        if (fs.existsSync(unpacked)) return unpacked;

        // Авто-извлечение бинарника из виртуального app.asar в кэш приложения
        try {
          const cachedBin = path.join(userBinDir(), binName);
          if (fs.existsSync(cachedBin) && fs.statSync(cachedBin).size > 10000) {
            return cachedBin;
          }
          if (fs.existsSync(p)) {
            const buf = fs.readFileSync(p);
            fs.mkdirSync(path.dirname(cachedBin), { recursive: true, mode: 0o700 });
            // Пишем во временный файл и переименовываем - иначе параллельный
            // запуск мог увидеть наполовину записанный бинарник.
            const tmpBin = `${cachedBin}.${process.pid}.tmp`;
            fs.writeFileSync(tmpBin, buf, { mode: 0o700 });
            fs.renameSync(tmpBin, cachedBin);
            return cachedBin;
          }
        } catch {
          // fallback
        }
      } else if (fs.existsSync(p)) {
        return p;
      }
    }
  } catch {
    // fallback
  }

  // Проверяем bundled в extraResources и кэш приложения
  const candidates = [
    path.join(process.resourcesPath || "", "app.asar.unpacked", "node_modules", "7zip-bin", process.platform, process.arch, binName)
    // path.join(process.env.LOCALAPPDATA || "", "Programs", "NanoDecompiler", "resources", "app.asar.unpacked", "node_modules", "7zip-bin", process.platform, process.arch, binName),
    // path.join(process.env.LOCALAPPDATA || "", "Programs", "nano-decompiler-gui", "resources", "app.asar.unpacked", "node_modules", "7zip-bin", process.platform, process.arch, binName),
    // path.join(userBinDir(), binName),
    // path.join(process.resourcesPath || "", "engine", "bin", binName),
    // path.join(process.resourcesPath || "", "engine", binName),
    // path.join(__dirname, "..", "node_modules", "7zip-bin", process.platform, process.arch, binName),
  ];

  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return null;
}

/** Автономная распаковка .zip архивов без внешних зависимостей (PowerShell Expand-Archive / system tar) */
async function extractZipFallback(
  archivePath: string,
  targetDir: string,
  onProgress: (ev: ArchiveProgressEvent) => void,
): Promise<void> {
  const stat = await fs.promises.stat(archivePath);
  const totalBytes = stat.size;

  onProgress({
    archivePath,
    percent: 30,
    currentFile: "Распаковка для анализа...",
    extractedFiles: 1,
    totalFiles: 10,
    etaSeconds: 2,
    speedBytesPerSec: 1024 * 1024 * 2,
    tempDir: targetDir,
  });

  // убрал shell: раньше команда собиралась строкой (`unzip -o "${archivePath}"`) и
  // исполнялась через /bin/sh - имя архива вида x$(cmd).zip или x"; cmd; ".zip
  // выполнялось как команда. execFile передаёт аргументы массивом, минуя shell.
  // Таймаут 10 минут (было 60 с и execSync - блокировал весь main-процесс).
  const FALLBACK_TIMEOUT_MS = 10 * 60 * 1000;
  if (process.platform === "win32") {
    const ps = `Expand-Archive -LiteralPath '${archivePath.replace(/'/g, "''")}' -DestinationPath '${targetDir.replace(/'/g, "''")}' -Force`;
    const encoded = Buffer.from(ps, "utf16le").toString("base64");
    await execFileAsync("powershell", ["-NoProfile", "-NonInteractive", "-EncodedCommand", encoded], {
      windowsHide: true,
      timeout: FALLBACK_TIMEOUT_MS,
    });
  } else {
    try {
      await execFileAsync("unzip", ["-o", "--", archivePath, "-d", targetDir], {
        timeout: FALLBACK_TIMEOUT_MS
      });
    } catch {
      await execFileAsync("tar", ["-xf", archivePath, "-C", targetDir], {
        timeout: FALLBACK_TIMEOUT_MS
      });
    }
  }
  if (await dirSizeExceeds(targetDir, MAX_EXTRACT_TOTAL_BYTES)) {
    throw new Error(".JAR файл либо ваш разархиватор сломан! пожалуйста отпишите владельцу для решения проблемы :) (t.me/radoqi)");
  }

  onProgress({
    archivePath,
    percent: 90,
    currentFile: "Распаковка файла завершена",
    extractedFiles: 10,
    totalFiles: 10,
    etaSeconds: 0,
    speedBytesPerSec: totalBytes,
    tempDir: targetDir,
  });
}

/**
 * Читает central directory JAR-файла и определяет, является ли он
 * плагином (Bukkit/Paper/Bungee/Velocity), серверным ядром или библиотекой.
 */
export async function inspectJarForPlugin(jarPath: string, rootDir: string): Promise<DiscoveredPlugin> {
  const fileName = path.basename(jarPath);
  const relPath = path.relative(rootDir, jarPath).replace(/\\/g, "/");
  let sizeBytes = 0;
  try {
    const stat = await fs.promises.stat(jarPath);
    sizeBytes = stat.size;
  } catch {
    // ignore
  }

  let summary: JarSummary | null = null;
  try {
    summary = await readJarSummaryNative(jarPath);
  } catch {
    // ignore
  }

  // Читаем список файлов внутри jar (общий читатель с проверкой границ - см. zipReader.ts)
  const entryNames: string[] = [];
  try {
    for (const e of await listZipEntries(jarPath)) entryNames.push(e.name);
  } catch {
    // fallback: не смогли прочитать central directory (ZIP64/битый файл) - классифицируем только по имени
  }

  // Set вместо entryNames.includes(): has() вызывается по ~10 раз на каждый jar,
  // а в больших jar десятки тысяч записей.
  const entrySet = new Set(entryNames);
  const has = (name: string) => entrySet.has(name);

  // 1. Проверка на ПЛАГИН (Bukkit / Paper / Bungee / Velocity / Sponge) — ПРИОРИТЕТ
  const hasPaperPlugin = has("paper-plugin.yml") || has("META-INF/paper-plugin.yml");
  const hasBukkitPlugin = has("plugin.yml");
  const hasBungeePlugin = has("bungee.yml") || has("waterfall.yml");
  const hasVelocityPlugin = has("velocity-plugin.json") || has("META-INF/velocity-plugin.json");
  const hasSpongePlugin = has("sponge_plugins.json") || has("META-INF/sponge_plugins.json");

  if (hasPaperPlugin || hasBukkitPlugin || hasBungeePlugin || hasVelocityPlugin || hasSpongePlugin) {
    let platform = "Bukkit/Spigot";
    if (hasPaperPlugin) platform = "Paper";
    else if (hasVelocityPlugin) platform = "Velocity";
    else if (hasBungeePlugin) platform = "BungeeCord";
    else if (hasSpongePlugin) platform = "Sponge";

    return {
      fileName,
      jarPath,
      relPath,
      sizeBytes,
      classCount: summary?.classes ?? null,
      pluginName: summary?.plugin_name ?? fileName.replace(/\.jar$/i, ""),
      pluginAuthor: summary?.plugin_author ?? null,
      platform,
      isPlugin: true,
      isServerCore: false,
    };
  }

  // 2. Проверка на СЕРВЕРНОЕ ЯДРО (Server Core) — только если нет манифеста плагина!
  // Важно: META-INF/versions/ есть во всех Java 9+ Multi-Release JAR (Kotlin, Netty и др. шейдируемые библиотеки) -
  // поэтому проверяем Paperclip только при явном наличии классов загрузчика io/papermc/paperclip или paperclip в пути.
  const isPaperclip = entryNames.some(e => e.startsWith("io/papermc/paperclip")) ||
    (entryNames.some(e => e.startsWith("META-INF/versions/")) && entryNames.some(e => e.toLowerCase().includes("paperclip")));
  const isBundler = entryNames.some(e => e.startsWith("net/minecraft/bundler/"));
  const isMinecraftServer = entryNames.some(e => e.startsWith("net/minecraft/server/MinecraftServer.class") || e.startsWith("net/minecraft/server/Main.class"));
  const isCraftBukkitCore = entryNames.some(e => e.startsWith("org/bukkit/craftbukkit/Main.class"));
  const isServerProperties = has("server.properties") || has("eula.txt");
  const lowerName = fileName.toLowerCase();
  const isServerName = /^(paper|spigot|purpur|folia|pufferfish|craftbukkit|server|minecraft_server|tuinity)-/i.test(lowerName) || lowerName === "server.jar";

  if (isPaperclip || isBundler || isMinecraftServer || isCraftBukkitCore || isServerProperties || isServerName) {
    let coreReason = "Обнаружены сигнатуры сервера Minecraft";
    if (isPaperclip) coreReason = "Paperclip загрузчик сервера (Paper/Purpur/Folia)";
    else if (isBundler) coreReason = "Vanilla / Bundler серверный загрузчик";
    else if (isMinecraftServer) coreReason = "Серверное ядро net.minecraft.server";
    else if (isCraftBukkitCore) coreReason = "Серверное ядро CraftBukkit / Spigot";
    else if (isServerProperties) coreReason = "Конфигурация сервера (server.properties/eula.txt)";
    else if (isServerName) coreReason = "Серверный архив ядра (не является плагином)";

    return {
      fileName,
      jarPath,
      relPath,
      sizeBytes,
      classCount: summary?.classes ?? null,
      pluginName: summary?.plugin_name ?? fileName,
      pluginAuthor: summary?.plugin_author ?? null,
      platform: "Серверное ядро",
      isPlugin: false,
      isServerCore: true,
      coreReason,
    };
  }

  // 3. Сторонняя библиотека или неизвестный jar без манифеста плагина
  return {
    fileName,
    jarPath,
    relPath,
    sizeBytes,
    classCount: summary?.classes ?? null,
    pluginName: summary?.plugin_name ?? null,
    pluginAuthor: summary?.plugin_author ?? null,
    platform: "Сторонняя библиотека (не плагин)",
    isPlugin: false,
    isServerCore: false,
    coreReason: "Отсутствует манифест плагина (plugin.yml / paper-plugin.yml / bungee.yml / velocity-plugin.json)",
  };
}

/** Рекурсивный поиск файлов .jar в папке */
async function scanJars(dir: string): Promise<string[]> {
  const result: string[] = [];
  const entries = await fs.promises.readdir(dir, { withFileTypes: true });
  for (const entry of entries) {
    const fullPath = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      const nested = await scanJars(fullPath);
      result.push(...nested);
    } else if (entry.isFile() && /\.jar$/i.test(entry.name)) {
      result.push(fullPath);
    }
  }
  return result;
}

/** Создает чистую временную папку для разархивации */
function createTempArchiveDir(archivePath: string): string {
  const base = path.basename(archivePath).replace(/[^a-zA-Z0-9_\-\.]/g, "_").slice(0, 80);
  const root = archivesRootDir();
  fs.mkdirSync(root, { recursive: true, mode: 0o700 });
  // mkdtemp добавляет случайный суффикс и создаёт каталог с правами 0700 -
  // раньше имя было `${Date.now()}_${base}` (предсказуемое: в общем /tmp его
  // можно занять заранее) и создавалось с правами по умолчанию.
  return fs.mkdtempSync(path.join(root, `${Date.now()}_${base}_`));
}

/** Распаковка RAR архивов через WebAssembly node-unrar-js */
async function extractRar(
  archivePath: string,
  targetDir: string,
  onProgress: (ev: ArchiveProgressEvent) => void,
): Promise<void> {
  const extractor = await unrar.createExtractorFromFile({
    filepath: archivePath,
    targetPath: targetDir,
  });

  const fileList = extractor.getFileList();
  const fileHeaders = [...fileList.fileHeaders];
  const totalFiles = fileHeaders.length;
  let totalBytes = 0;
  for (const h of fileHeaders) {
    totalBytes += h.unpSize || 0;
  }
  // Заголовки RAR читаются ДО распаковки - отказываем сразу, не создавая гигабайты на диске.
  if (totalFiles > MAX_EXTRACT_ENTRIES || totalBytes > MAX_EXTRACT_TOTAL_BYTES) {
    throw new Error("архив превышает допустимые лимиты (число файлов / распакованный размер)");
  }

  const startTime = Date.now();
  let extractedFiles = 0;
  let extractedBytes = 0;

  const extracted = extractor.extract();
  for (const file of extracted.files) {
    extractedFiles++;
    const unpSize = file.fileHeader?.unpSize || 0;
    extractedBytes += unpSize;
    const fileName = file.fileHeader?.name || `файл ${extractedFiles}`;

    const percent = totalFiles > 0 ? Math.min(99, Math.round((extractedFiles / totalFiles) * 100)) : 50;
    const elapsedSec = (Date.now() - startTime) / 1000;
    const speed = elapsedSec > 0 ? extractedBytes / elapsedSec : 0;
    const remainingRatio = totalFiles > 0 ? (totalFiles - extractedFiles) / extractedFiles : 0;
    const etaSeconds = Math.max(0, Math.ceil(elapsedSec * remainingRatio));

    onProgress({
      archivePath,
      percent,
      currentFile: fileName,
      extractedFiles,
      totalFiles,
      etaSeconds,
      speedBytesPerSec: speed,
      tempDir: targetDir,
    });
  }
}

/** Распаковка .tar и .tar.gz через node tar */
async function extractTar(
  archivePath: string,
  targetDir: string,
  onProgress: (ev: ArchiveProgressEvent) => void,
): Promise<void> {
  const stat = await fs.promises.stat(archivePath);
  const totalBytes = stat.size;
  const startTime = Date.now();
  let extractedFiles = 0;
  let extractedBytes = 0;

  let limitExceeded = false;
  let acceptedEntries = 0;
  let acceptedBytes = 0;

  await tar.x({
    file: archivePath,
    cwd: targetDir,
    // Ссылки не создаём вообще (symlink/hardlink в недоверенном архиве - классика
    // записи за пределы каталога), плюс лимиты на число записей и суммарный размер.
    filter: (_p, entry) => {
      const type = (entry as { type?: string }).type;
      if (type === "SymbolicLink" || type === "Link") return false;
      if (limitExceeded) return false;
      acceptedEntries++;
      acceptedBytes += (entry as { size?: number }).size || 0;
      if (acceptedEntries > MAX_EXTRACT_ENTRIES || acceptedBytes > MAX_EXTRACT_TOTAL_BYTES) {
        limitExceeded = true;
        return false;
      }
      return true;
    },
    onentry: (entry) => {
      extractedFiles++;
      extractedBytes += entry.size || 0;
      const elapsedSec = (Date.now() - startTime) / 1000;
      const speed = elapsedSec > 0 ? extractedBytes / elapsedSec : 0;
      const percent = totalBytes > 0 ? Math.min(99, Math.round((extractedBytes / totalBytes) * 100)) : 50;
      const remainingBytes = Math.max(0, totalBytes - extractedBytes);
      const etaSeconds = speed > 0 ? Math.ceil(remainingBytes / speed) : 2;

      onProgress({
        archivePath,
        percent,
        currentFile: entry.path,
        extractedFiles,
        totalFiles: Math.max(extractedFiles + 5, 20),
        etaSeconds,
        speedBytesPerSec: speed,
        tempDir: targetDir,
      });
    },
  });

  if (limitExceeded) {
    throw new Error("архив превышает допустимые лимиты (число файлов / распакованный размер)");
  }
}

/** Распаковка .7z, .zip или fallback через 7za */
async function extractWith7za(
  sevenZipExe: string,
  archivePath: string,
  targetDir: string,
  onProgress: (ev: ArchiveProgressEvent) => void,
): Promise<void> {
  return new Promise((resolve, reject) => {
    const startTime = Date.now();
    // "--" отделяет опции от имени архива: путь, начинающийся с "-", иначе
    // был бы воспринят 7za как ключ. Запуск БЕЗ shell (spawn с массивом).
    const args = ["x", "-y", "-bsp1", "-o" + targetDir, "--", archivePath];
    const proc = spawn(sevenZipExe, args, { windowsHide: true });

    // Сторож размера: у 7za нет лимита распаковки, архив-бомба иначе забьёт диск.
    let limitHit = false;
    let checking = false;
    const watchdog = setInterval(() => {
      if (checking || limitHit) return;
      checking = true;
      dirSizeExceeds(targetDir, MAX_EXTRACT_TOTAL_BYTES)
        .then((exceeded) => {
          if (exceeded) {
            limitHit = true;
            proc.kill();
          }
        })
        .catch(() => {})
        .finally(() => {
          checking = false;
        });
    }, 2000);
    let stderrTail = "";

    let lastPercent = 0;
    let currentFile = "Распаковка...";

    const parseLine = (line: string) => {
      // Ищем процент вида: " 45% 12/80 plugins/WorldEdit.jar"
      const m = /(\d{1,3})%\s*(?:(\d+)\/(\d+))?\s*(.*)/.exec(line);
      if (m) {
        const pct = parseInt(m[1], 10);
        if (!isNaN(pct)) lastPercent = pct;
        if (m[4] && m[4].trim()) currentFile = m[4].trim();

        const elapsedSec = (Date.now() - startTime) / 1000;
        let etaSeconds = 3;
        if (lastPercent > 0 && lastPercent < 100) {
          const totalEstimated = elapsedSec / (lastPercent / 100);
          etaSeconds = Math.max(0, Math.ceil(totalEstimated - elapsedSec));
        }

        onProgress({
          archivePath,
          percent: lastPercent,
          currentFile,
          extractedFiles: m[2] ? parseInt(m[2], 10) : 0,
          totalFiles: m[3] ? parseInt(m[3], 10) : 100,
          etaSeconds,
          speedBytesPerSec: 1024 * 1024,
          tempDir: targetDir,
        });
      }
    };

    proc.stdout.on("data", (d) => {
      const text = d.toString("utf-8");
      for (const line of text.split(/\r?\n/)) {
        if (line.trim()) parseLine(line);
      }
    });

    proc.stderr.on("data", (d) => {
      const text = d.toString("utf-8");
      stderrTail = (stderrTail + text).slice(-500);
      for (const line of text.split(/\r?\n/)) {
        if (line.trim()) parseLine(line);
      }
    });

    proc.on("close", (code) => {
      clearInterval(watchdog);
      if (limitHit) reject(new Error("распакованное содержимое превышает допустимый лимит размера"));
      else if (code === 0) resolve();
      else reject(new Error(`7-Zip завершился с кодом ошибки ${code}${stderrTail.trim() ? ": " + stderrTail.trim() : ""}`));
    });

    proc.on("error", (err) => {
      clearInterval(watchdog);
      reject(err);
    });
  });
}

/** Распаковывает архив и сканирует найденные jar-файлы */
export async function extractArchiveAndScanPlugins(
  archivePath: string,
  onProgress: (ev: ArchiveProgressEvent) => void,
): Promise<ArchiveExtractResult> {
  const tempDir = createTempArchiveDir(archivePath);
  const lower = archivePath.toLowerCase();

  try {
    onProgress({
      archivePath,
      percent: 5,
      currentFile: "Инициализация распаковки...",
      extractedFiles: 0,
      totalFiles: 100,
      etaSeconds: 5,
      speedBytesPerSec: 0,
      tempDir,
    });

    if (lower.endsWith(".rar")) {
      await extractRar(archivePath, tempDir, onProgress);
    } else if (lower.endsWith(".tar.gz") || lower.endsWith(".tgz") || lower.endsWith(".tar")) {
      await extractTar(archivePath, tempDir, onProgress);
    } else {
      // .zip, .7z, .7zip
      const sevenPath = get7zaPath();
      if (sevenPath) {
        try {
          await extractWith7za(sevenPath, archivePath, tempDir, onProgress);
        } catch (sevenErr) {
          if (lower.endsWith(".zip")) {
            await extractZipFallback(archivePath, tempDir, onProgress);
          } else {
            throw sevenErr;
          }
        }
      } else if (lower.endsWith(".zip")) {
        await extractZipFallback(archivePath, tempDir, onProgress);
      } else {
        throw new Error("Не найден модуль 7za для распаковки формата .7z");
      }
    }

    onProgress({
      archivePath,
      percent: 100,
      currentFile: "Поиск плагинов...",
      extractedFiles: 100,
      totalFiles: 100,
      etaSeconds: 0,
      speedBytesPerSec: 0,
      tempDir,
    });

    // Сканируем все найденные .jar файлы
    const jarPaths = await scanJars(tempDir);
    const plugins: DiscoveredPlugin[] = [];
    const serverCores: DiscoveredPlugin[] = [];
    let skippedNonPlugins = 0;

    for (const jp of jarPaths) {
      const info = await inspectJarForPlugin(jp, tempDir);
      if (info.isPlugin) {
        plugins.push(info);
      } else if (info.isServerCore) {
        serverCores.push(info);
      } else {
        skippedNonPlugins++;
      }
    }

    const fileStat = await fs.promises.stat(archivePath).catch(() => null);
    const archiveSizeBytes = fileStat ? fileStat.size : 0;

    return {
      ok: true,
      archivePath,
      tempDir,
      plugins,
      serverCores,
      skippedNonPlugins,
      totalJarsFound: jarPaths.length,
      archiveSizeBytes,
    };
  } catch (err) {
    // Неудачная/оборванная распаковка (в т.ч. по лимиту размера) не должна
    // оставлять полураспакованные файлы в %TEMP% - GUI не знает, что их надо чистить.
    await fs.promises.rm(tempDir, { recursive: true, force: true }).catch(() => {});
    const fileStat = await fs.promises.stat(archivePath).catch(() => null);
    const archiveSizeBytes = fileStat ? fileStat.size : 0;
    return {
      ok: false,
      archivePath,
      tempDir,
      plugins: [],
      serverCores: [],
      skippedNonPlugins: 0,
      totalJarsFound: 0,
      archiveSizeBytes,
      error: (err as Error).message || String(err),
    };
  }
}
