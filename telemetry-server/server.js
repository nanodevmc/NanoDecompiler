#!/usr/bin/env node
// НОВОЕ v1.8.4: мини-бэкенд телеметрии NanoDecompiler.
//
// Принимает POST /report от клиента (JSON, см. схему ниже), формирует
// человекочитаемый отчёт и пересылает его в Telegram-бота - коротким
// текстовым сообщением (summary) + файлом-вложением (полный контекст
// каждого отката на байткод: 10 строк до, сам байткод, 10 строк после -
// именно в таком порядке ДЛЯ КАЖДОГО отката, а не все "до" отдельно от
// всех "после").
//
// БЕЗ npm-зависимостей вообще - только встроенные модули Node.js (fetch и
// FormData глобальны с Node 18+). Специально: это разворачивается ОДИН РАЗ
// на сервере пользователя, минимум мороки - `node server.js` и всё.
//
// ---- Запуск ----
//   1. Скопировать config.example.json -> config.json, заполнить botToken/chatId
//      (или задать переменные окружения TG_BOT_TOKEN / TG_CHAT_ID / PORT -
//      они имеют приоритет над config.json).
//   2. node server.js
//   3. Указать http://<ip-сервера>:<port>/report как адрес телеметрии в
//      настройках NanoDecompiler (переменная окружения NANO_TELEMETRY_URL).
//
// ---- Безопасность (исправлено в 2.0.0-alpha) ----
//   - один кривой запрос больше не роняет процесс (раньше `null` в теле или
//     `heavy_files: [null]` приводили к необработанному исключению и падению);
//   - лимит по числу запросов с одного IP (RATE_LIMIT_PER_HOUR), таймауты на
//     заголовки/тело, лимит тела считается в БАЙТАХ;
//   - имена файлов внутри отправляемого ZIP очищаются от ".." и абсолютных
//     путей (zip-slip у того, кто распаковывает архив);
//   - секрет REPORT_SECRET сравнивается за постоянное время; клиент
//     отправляет его из переменной окружения NANO_TELEMETRY_SECRET;
//   - клиенту не возвращаются тексты ошибок Telegram;
//   - ВСЁ ЭТО ПО ПРОСТОМУ HTTP: отчёты содержат фрагменты декомпилированного
//     кода и читаются любым узлом на пути. Поставьте перед сервером
//     TLS-прокси (nginx/caddy) и используйте https://-адрес.
//
// ---- Как получить chatId для личных сообщений ----
//   Написать боту любое сообщение, затем открыть в браузере
//   https://api.telegram.org/bot<TOKEN>/getUpdates - там будет chat.id.

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

// ==================== pure node zip generator ====================
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

    // bit 11 (0x0800) - имя в UTF-8: без него кириллица/юникод в именах файлов
    // разные распаковщики читали в произвольной кодировке.
    const localHeader = Buffer.alloc(30);
    localHeader.writeUInt32LE(0x04034b50, 0);
    localHeader.writeUInt16LE(20, 4);
    localHeader.writeUInt16LE(0x0800, 6);
    localHeader.writeUInt16LE(8, 8);
    localHeader.writeUInt16LE(0, 10);
    localHeader.writeUInt16LE(0x21, 12); // дата 1980-01-01 (0 - невалидная дата в DOS-формате)
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

// ==================== конфигурация ====================

function loadConfig() {
  const configPath = path.join(__dirname, "config.json");
  let fileConfig = {};
  if (fs.existsSync(configPath)) {
    try {
      fileConfig = JSON.parse(fs.readFileSync(configPath, "utf8"));
    } catch (e) {
      console.error("config.json существует, но не парсится как JSON:", e.message);
      process.exit(1);
    }
  }
  const botToken = process.env.TG_BOT_TOKEN || fileConfig.botToken;
  const chatId = process.env.TG_CHAT_ID || fileConfig.chatId;
  const port = Number(process.env.PORT || fileConfig.port || 8787);
  const host = process.env.HOST || fileConfig.host || "0.0.0.0";
  // Необязательный общий секрет - клиент присылает его в заголовке
  // X-Report-Secret (из переменной окружения NANO_TELEMETRY_SECRET), сервер
  // отбрасывает запросы без совпадения. Пусто - проверка выключена.
  const reportSecret = process.env.REPORT_SECRET || fileConfig.reportSecret || "";
  // Сколько отчётов в час принимать с одного IP (0 - без ограничения).
  const rateLimitPerHour = Number(process.env.RATE_LIMIT_PER_HOUR || fileConfig.rateLimitPerHour || 30);
  // За обратным прокси брать IP клиента из последнего значения X-Forwarded-For.
  const trustProxy = String(process.env.TRUST_PROXY || fileConfig.trustProxy || "") === "true" || fileConfig.trustProxy === true;
  if (!botToken || !chatId) {
    console.error(
      "Не хватает botToken/chatId - заполни config.json (см. config.example.json) " +
        "или задай TG_BOT_TOKEN/TG_CHAT_ID в переменных окружения.",
    );
    process.exit(1);
  }
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    console.error("Некорректный порт: " + port);
    process.exit(1);
  }
  return {
    botToken,
    chatId: String(chatId),
    port,
    host,
    reportSecret,
    rateLimitPerHour: Number.isFinite(rateLimitPerHour) && rateLimitPerHour > 0 ? rateLimitPerHour : 0,
    trustProxy,
  };
}

const { botToken, chatId, port, host, reportSecret, rateLimitPerHour, trustProxy } = loadConfig();
const TG_API = `https://api.telegram.org/bot${botToken}`;

// Токен бота не должен попадать в логи, даже если он окажется в тексте ошибки.
function redact(s) {
  return String(s).split(botToken).join("***");
}

// ==================== нормализация входных данных ====================
// НОВОЕ: движок может прислать что угодно "странное" (не тот тип, NaN,
// отрицательные числа, слишком длинные массивы, null вместо объекта) - тут не
// отклоняем весь отчёт из-за одного кривого поля, а просто приводим к разумному виду.

function isObj(v) {
  return v !== null && typeof v === "object" && !Array.isArray(v);
}
function safeStr(v, fallback = "unknown") {
  if (typeof v === "string" && v.length) return v;
  if (typeof v === "number" && Number.isFinite(v)) return String(v);
  return fallback;
}
function safeNum(v, fallback = 0) {
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
}
function safeArrOfStr(v, maxItems = 2000, maxLineLen = 2000) {
  if (!Array.isArray(v)) return [];
  return v.slice(0, maxItems).map(x => {
    const s = typeof x === "string" ? x : safeStr(x, "");
    return s.length > maxLineLen ? s.slice(0, maxLineLen) + " …[обрезано]" : s;
  });
}

// Имя файла для ZIP: только относительный путь без "..", "." и управляющих
// символов, диска и ведущих слэшей (раньше убирались лишь ведущие слэши).
function safeZipPath(name) {
  const parts = String(name)
    .replace(/\\/g, "/")
    .split("/")
    .map(p => p.replace(/[\u0000-\u001f<>:"|?*]/g, "_").trim())
    .filter(p => p && p !== "." && p !== "..");
  const joined = parts.join("/").slice(0, 200);
  return joined || "file";
}

const MAX_HEAVY_FILE_CHARS = 2 * 1024 * 1024;

/**
 * @typedef {{file: string, method_hint: string, java_before: string[], bytecode: string[], java_after: string[]}} FallbackContext
 */

function normalizeReport(body) {
  const b = isObj(body) ? body : {};
  const fallbackContextsRaw = Array.isArray(b.fallback_contexts) ? b.fallback_contexts : [];
  const MAX_CONTEXTS = 200;
  const fallback_contexts = fallbackContextsRaw.slice(0, MAX_CONTEXTS).map(c => {
    const o = isObj(c) ? c : {};
    return {
      file: safeStr(o.file, "unknown file"),
      method_hint: safeStr(o.method_hint, ""),
      java_before: safeArrOfStr(o.java_before),
      bytecode: safeArrOfStr(o.bytecode),
      java_after: safeArrOfStr(o.java_after),
    };
  });

  const usedNames = new Set();
  const heavy_files = Array.isArray(b.heavy_files)
    ? b.heavy_files.slice(0, 20).filter(isObj).map(f => {
        let name = safeZipPath(safeStr(f.file, "unknown.java"));
        for (let i = 2; usedNames.has(name); i++) name = `${i}_${name}`;
        usedNames.add(name);
        const content = typeof f.content === "string" ? f.content.slice(0, MAX_HEAVY_FILE_CHARS) : "";
        return { file: name, content, fallback_pct: safeNum(f.fallback_pct, 0) };
      })
    : [];

  return {
    app_version: safeStr(b.app_version),
    app_commit: safeStr(b.app_commit, "unknown (not built from git checkout)"),
    engine_version: safeStr(b.engine_version),
    os: safeStr(b.os),
    target_plugin_name: safeStr(b.target_plugin_name, "(unknown plugin)"),
    target_platform: safeStr(b.target_platform, "unknown"),
    jar_file_name: safeStr(b.jar_file_name, "(unknown file)"),
    classes_total: Math.max(0, safeNum(b.classes_total)),
    total_methods: Math.max(0, safeNum(b.total_methods)),
    decompiled_methods: Math.max(0, safeNum(b.decompiled_methods)),
    fallback_methods: Math.max(0, safeNum(b.fallback_methods)),
    decompiled_pct: Math.max(0, Math.min(100, safeNum(b.decompiled_pct))),
    user_comment: safeStr(b.user_comment, "").slice(0, 8000),
    fallback_contexts,
    heavy_files,
  };
}

// ==================== форматирование отчёта ====================

function escapeHtml(s) {
  return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// Telegram режет сообщение по 4096 символов. Раньше текст обрезался по
// произвольному символу `slice(0, 4000)` - посреди тега/сущности, и Telegram
// отвечал 400 "can't parse entities". Теперь режем по границе строки (каждая
// строка содержит только закрытые теги), в запасе остаётся место под "…".
function joinWithinLimit(lines, limit = 3900) {
  const out = [];
  let len = 0;
  for (const line of lines) {
    if (len + line.length + 1 > limit) {
      out.push("…");
      break;
    }
    out.push(line);
    len += line.length + 1;
  }
  return out.join("\n");
}

function buildSummaryHtml(r) {
  const fallbackPct = r.total_methods > 0 ? (r.fallback_methods / r.total_methods) * 100 : (100 - r.decompiled_pct);
  const lines = [
    `<b>🔍 NanoDecompiler Report</b>`,
    ``,
    `📦 <b>Плагин:</b> <code>${escapeHtml(r.target_plugin_name)}</code> (${escapeHtml(r.jar_file_name)})`,
    `🖥 <b>Платформа:</b> <code>${escapeHtml(r.target_platform)}</code>`,
    `⚙️ <b>Версия:</b> GUI <code>${escapeHtml(r.app_version)}</code> · Engine <code>${escapeHtml(r.engine_version)}</code>`,
    `💻 <b>ОС:</b> <code>${escapeHtml(r.os)}</code>`,
    ``,
    `📊 <b>Статистика:</b>`,
    `• Классов: <b>${r.classes_total}</b>`,
    `• Методов: <b>${r.decompiled_methods}/${r.total_methods}</b> декомпилировано (<b>${r.decompiled_pct.toFixed(1)}%</b>)`,
    `• Байткод-откатов: <b>${r.fallback_methods}</b> (${fallbackPct.toFixed(1)}% | ${r.fallback_contexts.length} мест зафиксировано)`,
  ];

  if (r.user_comment) {
    lines.push(``, `💬 <b>Комментарий:</b>\n<blockquote expandable>${escapeHtml(r.user_comment.slice(0, 1500))}</blockquote>`);
  }

  if (r.fallback_contexts.length > 0) {
    lines.push(``, `⚠️ <b>Места байткода:</b>`);
    const previewCount = Math.min(3, r.fallback_contexts.length);
    for (let i = 0; i < previewCount; i++) {
      const c = r.fallback_contexts[i];
      const snippet = [
        ...c.java_before.slice(-5),
        `// --- БАЙТКОД (${c.bytecode.length} строк) ---`,
        ...c.bytecode.slice(0, 8),
        ...(c.bytecode.length > 8 ? ["// ..."] : []),
        ...c.java_after.slice(0, 5),
      ].join("\n");
      lines.push(
        `<b>[${i + 1}] ${escapeHtml(c.file)}</b>${c.method_hint ? ` · <code>${escapeHtml(c.method_hint)}</code>` : ""}\n<blockquote expandable>${escapeHtml(snippet.slice(0, 700))}</blockquote>`
      );
    }
    if (r.fallback_contexts.length > previewCount) {
      lines.push(`<i>... и ещё ${r.fallback_contexts.length - previewCount} мест во вложении</i>`);
    }
  }

  return joinWithinLimit(lines);
}

function buildPlainSummary(r) {
  return joinWithinLimit([
    "NanoDecompiler Report",
    `Плагин: ${r.target_plugin_name} (${r.jar_file_name})`,
    `Платформа: ${r.target_platform}`,
    `GUI ${r.app_version} / Engine ${r.engine_version}`,
    `Методов: ${r.decompiled_methods}/${r.total_methods} (${r.decompiled_pct.toFixed(1)}%), откатов: ${r.fallback_methods}`,
    r.user_comment ? `Комментарий: ${r.user_comment.slice(0, 1500)}` : "",
  ].filter(Boolean));
}

function buildAttachmentText(r) {
  const parts = [];
  parts.push(`NanoDecompiler error report`);
  parts.push(`====================================`);
  parts.push(`App version:    ${r.app_version}`);
  parts.push(`Commit:         ${r.app_commit}`);
  parts.push(`Engine version: ${r.engine_version}`);
  parts.push(`OS:             ${r.os}`);
  parts.push(``);
  parts.push(`Plugin:   ${r.target_plugin_name}`);
  parts.push(`Jar file: ${r.jar_file_name}`);
  parts.push(`Platform: ${r.target_platform}`);
  parts.push(``);
  parts.push(`Methods decompiled: ${r.decompiled_methods}/${r.total_methods} (${r.decompiled_pct.toFixed(1)}%)`);
  parts.push(`Fallback methods:   ${r.fallback_methods}`);
  if (r.user_comment) {
    parts.push(``);
    parts.push(`User comment:`);
    parts.push(r.user_comment);
  }
  parts.push(``);
  parts.push(`==================== FALLBACK LOCATIONS (${r.fallback_contexts.length}) ====================`);

  r.fallback_contexts.forEach((c, idx) => {
    parts.push(``);
    parts.push(`---- [${idx + 1}/${r.fallback_contexts.length}] ${c.file} ----`);
    if (c.method_hint) parts.push(`Method: ${c.method_hint}`);
    parts.push(``);
    parts.push(`-- ${Math.min(5, c.java_before.length)} line(s) before --`);
    parts.push(...c.java_before.slice(-5));
    parts.push(``);
    parts.push(`-- bytecode (${c.bytecode.length} line(s)) --`);
    parts.push(...c.bytecode);
    parts.push(``);
    parts.push(`-- ${Math.min(5, c.java_after.length)} line(s) after --`);
    parts.push(...c.java_after.slice(0, 5));
  });

  return parts.join("\n");
}

// ==================== отправка в Telegram ====================

// Telegram Bot API: документ - до 50 МБ.
const TG_MAX_DOCUMENT_BYTES = 45 * 1024 * 1024;

async function tgSendMessage(text, html) {
  const payload = { chat_id: chatId, text };
  if (html) payload.parse_mode = "HTML";
  const res = await fetch(`${TG_API}/sendMessage`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(payload),
    signal: AbortSignal.timeout(30000),
  });
  if (!res.ok) throw new Error(`sendMessage: ${res.status} ${await res.text()}`);
}

async function tgSendDocument(filename, content, mimeType = "text/plain; charset=utf-8") {
  if (!content) return;
  const form = new FormData();
  form.append("chat_id", chatId);
  form.append("document", new Blob([content], { type: mimeType }), filename);
  const res = await fetch(`${TG_API}/sendDocument`, { method: "POST", body: form, signal: AbortSignal.timeout(120000) });
  if (!res.ok) throw new Error(`sendDocument: ${res.status} ${await res.text()}`);
}

// ==================== защита от злоупотреблений ====================

const hits = new Map(); // ip -> [timestamps]
function rateLimited(ip) {
  if (!rateLimitPerHour) return false;
  const now = Date.now();
  const windowStart = now - 60 * 60 * 1000;
  const list = (hits.get(ip) || []).filter(t => t > windowStart);
  if (list.length >= rateLimitPerHour) {
    hits.set(ip, list);
    return true;
  }
  list.push(now);
  hits.set(ip, list);
  return false;
}
// Чтобы Map не рос бесконечно от разных адресов.
setInterval(() => {
  const windowStart = Date.now() - 60 * 60 * 1000;
  for (const [ip, list] of hits) {
    const fresh = list.filter(t => t > windowStart);
    if (fresh.length) hits.set(ip, fresh);
    else hits.delete(ip);
  }
}, 10 * 60 * 1000).unref();

function clientIp(req) {
  if (trustProxy) {
    const xff = String(req.headers["x-forwarded-for"] || "").split(",").map(s => s.trim()).filter(Boolean);
    if (xff.length) return xff[xff.length - 1];
  }
  return req.socket.remoteAddress || "unknown";
}

function secretMatches(provided) {
  if (!reportSecret) return true;
  if (typeof provided !== "string") return false;
  const a = crypto.createHash("sha256").update(provided).digest();
  const b = crypto.createHash("sha256").update(reportSecret).digest();
  return crypto.timingSafeEqual(a, b);
}

function reply(res, status, obj) {
  if (res.headersSent || res.writableEnded) return;
  const text = typeof obj === "string" ? obj : JSON.stringify(obj);
  res.writeHead(status, { "content-type": typeof obj === "string" ? "text/plain; charset=utf-8" : "application/json" }).end(text);
}

// ==================== HTTP-сервер ====================

const MAX_BODY = 25 * 1024 * 1024;
let inFlight = 0;
const MAX_IN_FLIGHT = 8;

async function handleReport(body) {
  const report = normalizeReport(body);
  try {
    await tgSendMessage(buildSummaryHtml(report), true);
  } catch (e) {
    // Типичная причина - 400 "can't parse entities": повторяем простым текстом.
    if (/\b400\b/.test(String(e.message))) await tgSendMessage(buildPlainSummary(report), false);
    else throw e;
  }
  const safeName = report.jar_file_name.replace(/[^\w.\-]+/g, "_").slice(0, 60) || "report";
  const fallbackPct = report.total_methods > 0 ? (report.fallback_methods / report.total_methods) * 100 : (100 - report.decompiled_pct);

  if (fallbackPct > 5.0 || report.heavy_files.length > 0) {
    // Упаковываем в ZIP архив, если байткод превышает 5% или есть файлы с байткодом
    const stats = { ...report, heavy_files: report.heavy_files.map(f => ({ file: f.file, fallback_pct: f.fallback_pct })) };
    const zipFiles = [
      { name: "report_summary.txt", data: buildAttachmentText(report) },
      { name: "stats.json", data: JSON.stringify(stats, null, 2) },
    ];
    for (const f of report.heavy_files) {
      zipFiles.push({ name: `sources/${f.file}`, data: f.content });
    }
    const zipBuf = createZip(zipFiles);
    if (zipBuf.length <= TG_MAX_DOCUMENT_BYTES) {
      await tgSendDocument(`${safeName}_bytecode_archive.zip`, zipBuf, "application/zip");
    } else {
      await tgSendDocument(`${safeName}_fallback_report.txt`, buildAttachmentText(report), "text/plain; charset=utf-8");
    }
  } else if (report.fallback_contexts.length > 0) {
    await tgSendDocument(`${safeName}_fallback_report.txt`, buildAttachmentText(report), "text/plain; charset=utf-8");
  }
}

const server = http.createServer((req, res) => {
  if (req.method === "GET" && req.url === "/health") {
    reply(res, 200, { ok: true });
    return;
  }
  if (req.method !== "POST" || req.url !== "/report") {
    reply(res, 404, "not found");
    return;
  }
  if (!secretMatches(req.headers["x-report-secret"])) {
    reply(res, 401, "unauthorized");
    return;
  }
  if (rateLimited(clientIp(req))) {
    reply(res, 429, "too many requests");
    return;
  }
  if (inFlight >= MAX_IN_FLIGHT) {
    reply(res, 503, "busy");
    return;
  }

  const declared = Number(req.headers["content-length"]);
  if (Number.isFinite(declared) && declared > MAX_BODY) {
    reply(res, 413, "payload too large");
    req.destroy();
    return;
  }

  inFlight++;
  let released = false;
  const release = () => {
    if (!released) {
      released = true;
      inFlight--;
    }
  };
  res.on("close", release);

  const chunks = [];
  let size = 0;
  let tooBig = false;
  req.on("data", chunk => {
    if (tooBig) return;
    size += chunk.length; // байты, а не символы
    if (size > MAX_BODY) {
      tooBig = true;
      reply(res, 413, "payload too large");
      req.destroy();
      return;
    }
    chunks.push(chunk);
  });
  req.on("error", release);
  req.on("end", async () => {
    if (tooBig) return;
    try {
      let body;
      try {
        body = JSON.parse(Buffer.concat(chunks).toString("utf8"));
      } catch {
        reply(res, 400, "invalid json");
        return;
      }
      if (!isObj(body)) {
        reply(res, 400, "json object expected");
        return;
      }
      await handleReport(body);
      reply(res, 200, { ok: true });
    } catch (e) {
      // Подробности (в т.ч. ответ Telegram) - только в лог, клиенту не отдаём.
      console.error("Ошибка обработки отчёта:", redact(e && e.message ? e.message : e));
      reply(res, 502, { ok: false, error: "upstream error" });
    } finally {
      release();
    }
  });
});

// Таймауты против медленных клиентов (slowloris): раньше не было никаких.
server.headersTimeout = 15000;
server.requestTimeout = 60000;
server.keepAliveTimeout = 5000;
server.maxHeadersCount = 50;

// Последний рубеж: необработанный отказ промиса логируется, а не роняет сервис
// (раньше так падал любой запрос с кривым JSON). uncaughtException намеренно НЕ
// глушим - после него состояние процесса неопределённо, пусть перезапустит supervisor.
process.on("unhandledRejection", e => console.error("unhandledRejection:", redact(e && e.stack ? e.stack : e)));

server.listen(port, host, () => {
  console.log(`NanoDecompiler telemetry relay слушает на ${host}:${port} (POST /report)`);
  if (!reportSecret) console.log("ВНИМАНИЕ: REPORT_SECRET не задан - сервер принимает отчёты от кого угодно.");
});
