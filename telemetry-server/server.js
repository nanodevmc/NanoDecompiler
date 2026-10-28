#!/usr/bin/env node
/**
 * NanoDecompiler Telemetry & Bug Report Server v2.1.0
 * 
 * Особенности:
 * 1. Валидация ключа подтверждения: sha256(base64(shortCommit + "+" + version)).
 * 2. Строгая схема: отклонение неизвестных параметров и отчет о недостающих.
 * 3. Типы жалоб: hang, crash, gui_error, decompilation_fallback, user_feedback.
 * 4. Персистентная память по прошлым жалобам (reports_memory.json).
 * 5. Бот с командами: /stats, /recent, /memory, /clear.
 * 6. Полная поддержка логов зависаний (до 300 строк), файла и байткода.
 */

"use strict";

const http = require("http");
const fs = require("fs");
const path = require("path");
const zlib = require("zlib");
const crypto = require("crypto");

if (Number(process.versions.node.split(".")[0]) < 18) {
  console.error("Нужен Node.js 18+ (глобальные fetch/FormData/Blob). Сейчас: " + process.version);
  process.exit(1);
}

// ==================== Конфигурация ====================

function loadConfig() {
  const configPath = path.join(__dirname, "config.json");
  let fileConfig = {};
  if (fs.existsSync(configPath)) {
    try {
      fileConfig = JSON.parse(fs.readFileSync(configPath, "utf8"));
    } catch (e) {
      console.error("config.json ошибка парсинга:", e.message);
    }
  }
  const botToken = process.env.TG_BOT_TOKEN || fileConfig.botToken || "";
  const chatId = String(process.env.TG_CHAT_ID || fileConfig.chatId || "");
  const port = Number(process.env.PORT || fileConfig.port || 8787);
  const host = process.env.HOST || fileConfig.host || "0.0.0.0";
  const reportSecret = process.env.REPORT_SECRET || fileConfig.reportSecret || "";
  const rateLimitPerHour = Number(process.env.RATE_LIMIT_PER_HOUR || fileConfig.rateLimitPerHour || 60);
  const trustProxy = String(process.env.TRUST_PROXY || fileConfig.trustProxy || "") === "true" || fileConfig.trustProxy === true;

  return { botToken, chatId, port, host, reportSecret, rateLimitPerHour, trustProxy };
}

const config = loadConfig();
const TG_API = config.botToken ? `https://api.telegram.org/bot${config.botToken}` : null;

// ==================== Zip Generator ====================

const crcTable = new Uint32Array(256);
for (let n = 0; n < 256; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) {
    c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
  }
  crcTable[n] = c;
}

function crc32(buf) {
  let crc = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    crc = crcTable[(crc ^ buf[i]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function createZip(files) {
  const localHeaders = [];
  const centralHeaders = [];
  let offset = 0;

  for (const file of files) {
    const nameBuf = Buffer.from(file.name, "utf8");
    const dataBuf = Buffer.isBuffer(file.data) ? file.data : Buffer.from(file.data || "", "utf8");
    const crc = crc32(dataBuf);
    const compressed = zlib.deflateRawSync(dataBuf);
    const uncompressedSize = dataBuf.length;
    const compressedSize = compressed.length;

    const localHeader = Buffer.alloc(30);
    localHeader.writeUInt32LE(0x04034b50, 0);
    localHeader.writeUInt16LE(20, 4);
    localHeader.writeUInt16LE(0x0800, 6);
    localHeader.writeUInt16LE(8, 8);
    localHeader.writeUInt16LE(0, 10);
    localHeader.writeUInt16LE(0x21, 12);
    localHeader.writeUInt32LE(crc, 14);
    localHeader.writeUInt32LE(compressedSize, 18);
    localHeader.writeUInt32LE(uncompressedSize, 22);
    localHeader.writeUInt16LE(nameBuf.length, 26);
    localHeader.writeUInt16LE(0, 28);

    localHeaders.push(localHeader, nameBuf, compressed);

    const centralHeader = Buffer.alloc(46);
    centralHeader.writeUInt32LE(0x02014b50, 0);
    centralHeader.writeUInt16LE(20, 4);
    centralHeader.writeUInt16LE(20, 6);
    centralHeader.writeUInt16LE(0x0800, 8);
    centralHeader.writeUInt16LE(8, 10);
    centralHeader.writeUInt16LE(0, 12);
    centralHeader.writeUInt16LE(0x21, 14);
    centralHeader.writeUInt32LE(crc, 16);
    centralHeader.writeUInt32LE(compressedSize, 20);
    centralHeader.writeUInt32LE(uncompressedSize, 24);
    centralHeader.writeUInt16LE(nameBuf.length, 28);
    centralHeader.writeUInt16LE(0, 30);
    centralHeader.writeUInt16LE(0, 32);
    centralHeader.writeUInt16LE(0, 34);
    centralHeader.writeUInt16LE(0, 36);
    centralHeader.writeUInt32LE(0, 38);
    centralHeader.writeUInt32LE(offset, 42);

    centralHeaders.push(centralHeader, nameBuf);
    offset += 30 + nameBuf.length + compressedSize;
  }

  const centralDirOffset = offset;
  const centralDirSize = centralHeaders.reduce((acc, b) => acc + b.length, 0);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(0, 4);
  eocd.writeUInt16LE(0, 6);
  eocd.writeUInt16LE(files.length, 8);
  eocd.writeUInt16LE(files.length, 10);
  eocd.writeUInt32LE(centralDirSize, 12);
  eocd.writeUInt32LE(centralDirOffset, 16);
  eocd.writeUInt16LE(0, 20);

  return Buffer.concat([...localHeaders, ...centralHeaders, eocd]);
}

// ==================== База памяти (reports_memory.json) ====================

const MEMORY_FILE = path.join(__dirname, "reports_memory.json");
let memoryStore = {
  total_reports: 0,
  incidents: {}, // fingerprint -> { count, first_seen, last_seen, type, title }
  recent: [],
};

function loadMemory() {
  if (fs.existsSync(MEMORY_FILE)) {
    try {
      memoryStore = JSON.parse(fs.readFileSync(MEMORY_FILE, "utf8"));
    } catch (e) {
      console.warn("Не удалось прочитать memory file, сброс:", e.message);
    }
  }
}

function saveMemory() {
  try {
    fs.writeFileSync(MEMORY_FILE, JSON.stringify(memoryStore, null, 2), "utf8");
  } catch (e) {
    console.error("Ошибка сохранения памяти:", e.message);
  }
}

loadMemory();

function recordIncident(report) {
  memoryStore.total_reports++;
  const rawKey = `${report.report_type}:${report.target_plugin_name || report.error_type || "app"}:${report.current_file || report.error_message || ""}`;
  const fp = crypto.createHash("sha256").update(rawKey).digest("hex").slice(0, 16);
  const now = new Date().toISOString();

  let entry = memoryStore.incidents[fp];
  if (!entry) {
    entry = {
      fingerprint: fp,
      type: report.report_type,
      title: report.error_message || report.target_plugin_name || "Unknown issue",
      count: 1,
      first_seen: now,
      last_seen: now,
    };
    memoryStore.incidents[fp] = entry;
  } else {
    entry.count++;
    entry.last_seen = now;
  }

  memoryStore.recent.unshift({
    timestamp: now,
    type: report.report_type,
    fingerprint: fp,
    title: entry.title,
    app_version: report.app_version,
  });

  if (memoryStore.recent.length > 50) {
    memoryStore.recent = memoryStore.recent.slice(0, 50);
  }

  saveMemory();
  return entry;
}

// ==================== Схема и валидация ====================

const ALLOWED_FIELDS = new Set([
  "report_type",
  "app_version",
  "app_commit",
  "engine_version",
  "os",
  "timestamp",
  "auth_token",
  "target_plugin_name",
  "target_platform",
  "jar_file_name",
  "classes_total",
  "total_methods",
  "decompiled_methods",
  "fallback_methods",
  "decompiled_pct",
  "user_comment",
  "fallback_contexts",
  "heavy_files",
  "error_type",
  "error_message",
  "error_stack",
  "last_logs",
  "current_file",
  "current_bytecode",
]);

const REQUIRED_FIELDS = [
  "report_type",
  "app_version",
  "app_commit",
  "os",
];

const VALID_REPORT_TYPES = new Set([
  "hang",
  "crash",
  "gui_error",
  "decompilation_fallback",
  "user_feedback",
]);

function computeAuthToken(shortCommit, version) {
  const short = String(shortCommit || "").trim().slice(0, 7);
  const ver = String(version || "").trim();
  const raw = `${short}+${ver}`;
  const b64 = Buffer.from(raw, "utf8").toString("base64");
  return crypto.createHash("sha256").update(b64, "utf8").digest("hex");
}

function validatePayload(body, headers) {
  if (!body || typeof body !== "object" || Array.isArray(body)) {
    return { ok: false, status: 400, error: "invalid_json_object" };
  }

  // 1. Проверка лишних полей
  const extraFields = Object.keys(body).filter(k => !ALLOWED_FIELDS.has(k));
  if (extraFields.length > 0) {
    return {
      ok: false,
      status: 400,
      error: "unexpected_fields",
      details: `Запрос отклонен: обнаружены лишние параметры: ${extraFields.join(", ")}`,
      extra: extraFields,
    };
  }

  // 2. Проверка недостающих обязательных полей
  const missingFields = REQUIRED_FIELDS.filter(k => body[k] === undefined || body[k] === null || body[k] === "");
  if (missingFields.length > 0) {
    return {
      ok: false,
      status: 400,
      error: "missing_fields",
      details: `Запрос не полон: отсутствуют обязательные поля: ${missingFields.join(", ")}`,
      missing: missingFields,
    };
  }

  // 3. Проверка типа жалобы
  if (!VALID_REPORT_TYPES.has(body.report_type)) {
    return {
      ok: false,
      status: 400,
      error: "invalid_report_type",
      details: `Недопустимый тип жалобы: ${body.report_type}. Допустимы: ${Array.from(VALID_REPORT_TYPES).join(", ")}`,
    };
  }

  // 4. Валидация ключа подтверждения: sha256(base64(shortCommit + "+" + version))
  const expectedToken = computeAuthToken(body.app_commit, body.app_version);
  const providedToken = String(headers["x-report-token"] || body.auth_token || headers["x-report-secret"] || "").trim();

  // Если задан REPORT_SECRET в конфиге сервера, также принимаем его
  const legacySecretOk = config.reportSecret && providedToken === config.reportSecret;

  if (providedToken !== expectedToken && !legacySecretOk) {
    return {
      ok: false,
      status: 401,
      error: "unauthorized_token",
      details: "Ключ подтверждения отправки не совпадает с sha256(base64(commit+version))",
    };
  }

  return { ok: true };
}

// ==================== Telegram форматирование ====================

function escapeHtml(s) {
  return String(s || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function buildTelegramMessage(report, memoryEntry) {
  const typeIcons = {
    hang: "⏳ ЗАВИСАНИЕ",
    crash: "💥 ВЗРЫВ / КРАШ",
    gui_error: "⚠️ ОШИБКА GUI",
    decompilation_fallback: "🔍 БАЙТКОД-ОТКАТ",
    user_feedback: "💬 ОТЗЫВ ПОЛЬЗОВАТЕЛЯ",
  };

  const lines = [
    `<b>${typeIcons[report.report_type] || "🐞 БАГ-РЕПОРТ"}</b>`,
    ``,
    `⚙️ <b>Версия:</b> <code>${escapeHtml(report.app_version)}</code> (${escapeHtml(String(report.app_commit).slice(0, 7))})`,
    `💻 <b>ОС:</b> <code>${escapeHtml(report.os)}</code>`,
  ];

  if (memoryEntry && memoryEntry.count > 1) {
    lines.push(`🔁 <b>Повторяемость:</b> замечен <b>${memoryEntry.count}-й раз</b> (первый: ${memoryEntry.first_seen.slice(0, 16).replace("T", " ")})`);
  }

  if (report.target_plugin_name) {
    lines.push(`📦 <b>Плагин:</b> <code>${escapeHtml(report.target_plugin_name)}</code> (${escapeHtml(report.jar_file_name || "")})`);
  }

  if (report.current_file) {
    lines.push(`📄 <b>Текущий файл:</b> <code>${escapeHtml(report.current_file)}</code>`);
  }

  if (report.error_message) {
    lines.push(``, `❌ <b>Ошибка:</b>\n<blockquote expandable>${escapeHtml(report.error_message.slice(0, 1000))}</blockquote>`);
  }

  if (report.user_comment) {
    lines.push(``, `💬 <b>Комментарий:</b>\n<blockquote expandable>${escapeHtml(report.user_comment.slice(0, 1500))}</blockquote>`);
  }

  if (report.decompiled_pct !== undefined) {
    lines.push(
      ``,
      `📊 <b>Декомпиляция:</b> <b>${report.decompiled_methods || 0}/${report.total_methods || 0}</b> (${Number(report.decompiled_pct || 0).toFixed(1)}%) · Откатов: <b>${report.fallback_methods || 0}</b>`
    );
  }

  if (report.last_logs && Array.isArray(report.last_logs) && report.last_logs.length > 0) {
    const previewLogs = report.last_logs.slice(-10).join("\n");
    lines.push(``, `📋 <b>Хвост логов (последние 10 строк из ${report.last_logs.length}):</b>\n<pre>${escapeHtml(previewLogs.slice(0, 800))}</pre>`);
  }

  return lines.join("\n");
}

async function tgSendMessage(text, html = true) {
  if (!TG_API || !config.chatId) return;
  const payload = { chat_id: config.chatId, text };
  if (html) payload.parse_mode = "HTML";
  const res = await fetch(`${TG_API}/sendMessage`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(payload),
    signal: AbortSignal.timeout(30000),
  });
  if (!res.ok) throw new Error(`tgSendMessage: ${res.status} ${await res.text()}`);
}

async function tgSendDocument(filename, buffer, mimeType = "application/zip") {
  if (!TG_API || !config.chatId || !buffer) return;
  const form = new FormData();
  form.append("chat_id", config.chatId);
  form.append("document", new Blob([buffer], { type: mimeType }), filename);
  const res = await fetch(`${TG_API}/sendDocument`, {
    method: "POST",
    body: form,
    signal: AbortSignal.timeout(120000),
  });
  if (!res.ok) throw new Error(`tgSendDocument: ${res.status} ${await res.text()}`);
}

// ==================== Telegram Bot Команды ====================

let lastUpdateId = 0;
async function pollTelegramUpdates() {
  if (!TG_API) return;
  try {
    const res = await fetch(`${TG_API}/getUpdates?offset=${lastUpdateId + 1}&timeout=20`, {
      signal: AbortSignal.timeout(35000),
    });
    if (!res.ok) return;
    const data = await res.json();
    if (!data.ok || !Array.isArray(data.result)) return;

    for (const update of data.result) {
      lastUpdateId = Math.max(lastUpdateId, update.update_id);
      const msg = update.message;
      if (!msg || !msg.text) continue;
      const text = msg.text.trim();
      const replyChatId = msg.chat.id;

      const sendReply = async (replyText) => {
        await fetch(`${TG_API}/sendMessage`, {
          method: "POST",
          headers: { "content-type": "application/json" },
          body: JSON.stringify({ chat_id: replyChatId, text: replyText, parse_mode: "HTML" }),
        });
      };

      if (text === "/stats") {
        const typesCount = {};
        for (const inc of Object.values(memoryStore.incidents)) {
          typesCount[inc.type] = (typesCount[inc.type] || 0) + inc.count;
        }
        const statsMsg = [
          `📊 <b>Статистика NanoDecompiler Telemetry</b>`,
          `• Всего принято репортов: <b>${memoryStore.total_reports}</b>`,
          `• Уникальных инцидентов: <b>${Object.keys(memoryStore.incidents).length}</b>`,
          ``,
          `<b>По категориям:</b>`,
          ...Object.entries(typesCount).map(([t, c]) => `• ${t}: <b>${c}</b>`),
        ].join("\n");
        await sendReply(statsMsg);
      } else if (text === "/recent") {
        const recentLines = memoryStore.recent.slice(0, 8).map((r, i) =>
          `<b>${i + 1}.</b> [${r.type}] <code>${escapeHtml(r.title.slice(0, 40))}</code> (${r.timestamp.slice(11, 16)})`
        );
        await sendReply(recentLines.length ? recentLines.join("\n") : "Нет недавних инцидентов.");
      } else if (text === "/memory") {
        const topIncidents = Object.values(memoryStore.incidents)
          .sort((a, b) => b.count - a.count)
          .slice(0, 5)
          .map((inc, i) => `<b>#${i + 1} (${inc.count}x):</b> [${inc.type}] <code>${escapeHtml(inc.title.slice(0, 50))}</code>`);
        await sendReply(topIncidents.length ? `🧠 <b>Топ инцидентов из памяти:</b>\n\n` + topIncidents.join("\n") : "Память пуста.");
      } else if (text === "/clear") {
        memoryStore = { total_reports: 0, incidents: {}, recent: [] };
        saveMemory();
        await sendReply("🧹 Память жалоб успешно очищена.");
      }
    }
  } catch (e) {
    // Тихо продолжаем polling
  }
}

if (TG_API) {
  setInterval(pollTelegramUpdates, 5000);
}

// ==================== HTTP Сервер ====================

const rateLimitHits = new Map();
function isRateLimited(ip) {
  if (!config.rateLimitPerHour) return false;
  const now = Date.now();
  const windowStart = now - 60 * 60 * 1000;
  const list = (rateLimitHits.get(ip) || []).filter(t => t > windowStart);
  if (list.length >= config.rateLimitPerHour) {
    rateLimitHits.set(ip, list);
    return true;
  }
  list.push(now);
  rateLimitHits.set(ip, list);
  return false;
}

const server = http.createServer(async (req, res) => {
  const sendJson = (status, data) => {
    if (res.headersSent) return;
    res.writeHead(status, { "Content-Type": "application/json; charset=utf-8" });
    res.end(JSON.stringify(data));
  };

  if (req.method === "GET" && req.url === "/health") {
    return sendJson(200, { ok: true, version: "2.1.0", total_reports: memoryStore.total_reports });
  }

  if (req.method !== "POST" || req.url !== "/report") {
    return sendJson(404, { ok: false, error: "not_found" });
  }

  const clientIp = req.socket.remoteAddress || "unknown";
  if (isRateLimited(clientIp)) {
    return sendJson(429, { ok: false, error: "too_many_requests" });
  }

  const chunks = [];
  let bytesCount = 0;
  const MAX_BYTES = 50 * 1024 * 1024;

  req.on("data", chunk => {
    bytesCount += chunk.length;
    if (bytesCount > MAX_BYTES) {
      sendJson(413, { ok: false, error: "payload_too_large" });
      req.destroy();
      return;
    }
    chunks.push(chunk);
  });

  req.on("end", async () => {
    try {
      const raw = Buffer.concat(chunks).toString("utf8");
      let body;
      try {
        body = JSON.parse(raw);
      } catch (err) {
        return sendJson(400, { ok: false, error: "invalid_json" });
      }

      // Валидация схемы и ключа
      const val = validatePayload(body, req.headers);
      if (!val.ok) {
        return sendJson(val.status, { ok: false, error: val.error, details: val.details, extra: val.extra, missing: val.missing });
      }

      // Запись в память
      const incident = recordIncident(body);

      // Формирование Telegram сообщения
      const msgHtml = buildTelegramMessage(body, incident);
      await tgSendMessage(msgHtml, true);

      // Формирование архива при необходимости (hang, crash, тяжелые логи или байткод)
      const zipFiles = [];

      // 1. Логи
      if (body.last_logs && Array.isArray(body.last_logs) && body.last_logs.length > 0) {
        zipFiles.push({ name: "logs_last_300.txt", data: body.last_logs.join("\n") });
      }

      // 2. Текущий файл и байткод при зависании / краше
      if (body.current_file) {
        const safeFileName = path.basename(body.current_file);
        if (body.current_bytecode) {
          zipFiles.push({ name: `${safeFileName}.bytecode.txt`, data: String(body.current_bytecode) });
        }
      }

      // 3. Fallback контексты
      if (body.fallback_contexts && Array.isArray(body.fallback_contexts) && body.fallback_contexts.length > 0) {
        zipFiles.push({ name: "fallback_contexts.json", data: JSON.stringify(body.fallback_contexts, null, 2) });
      }

      // 4. Тяжелые файлы декомпиляции
      if (body.heavy_files && Array.isArray(body.heavy_files)) {
        for (const hf of body.heavy_files) {
          if (hf && hf.file && hf.content) {
            zipFiles.push({ name: `sources/${path.basename(hf.file)}`, data: hf.content });
          }
        }
      }

      // 5. Метаданные инцидента
      zipFiles.push({ name: "incident_meta.json", data: JSON.stringify({ ...body, incident_record: incident }, null, 2) });

      if (zipFiles.length > 0) {
        const zip = createZip(zipFiles);
        const prefix = body.report_type || "report";
        const filename = `${prefix}_${body.target_plugin_name || "app"}_${Date.now()}.zip`;
        await tgSendDocument(filename, zip, "application/zip");
      }

      return sendJson(200, { ok: true, status: "reported", fingerprint: incident.fingerprint, incident_count: incident.count });
    } catch (e) {
      console.error("Ошибка обработки /report:", e);
      return sendJson(500, { ok: false, error: "internal_error", details: e.message });
    }
  });
});

server.listen(config.port, config.host, () => {
  console.log(`[TelemetryServer] Сервер запущен на ${config.host}:${config.port}`);
  console.log(`[TelemetryServer] Бот: ${config.botToken ? "Подключен" : "Выключен (нет токена)"}, ChatId: ${config.chatId || "нет"}`);
});
