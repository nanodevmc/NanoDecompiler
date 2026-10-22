import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import * as https from "https";
import { execFile } from "child_process";
import { promisify } from "util";
import { listZipEntries, readZipEntry, type ZipEntry } from "./zipReader";

const execFileAsync = promisify(execFile);

// plugin.yml - крошечный файл; больше не читаем целиком (zip-bomb в jar).
const MAX_PLUGIN_YML_BYTES = 1024 * 1024;

export async function compareJars(jarA: string, jarB: string) {
  try {
    const [entriesA, entriesB] = await Promise.all([listZipEntries(jarA), listZipEntries(jarB)]);

    const mapA = new Map<string, ZipEntry>();
    for (const e of entriesA) {
      if (e.name.endsWith(".class")) mapA.set(e.name, e);
    }

    const mapB = new Map<string, ZipEntry>();
    for (const e of entriesB) {
      if (e.name.endsWith(".class")) mapB.set(e.name, e);
    }

    const added: string[] = [];
    const removed: string[] = [];
    const modified: Array<{ class: string; size_old: number; size_new: number; size_diff: number }> = [];
    let identicalCount = 0;

    for (const [name, entryB] of mapB.entries()) {
      const entryA = mapA.get(name);
      if (!entryA) {
        added.push(name);
      } else {
        if (entryA.crc32 === entryB.crc32 && entryA.uncompSize === entryB.uncompSize) {
          identicalCount++;
        } else {
          modified.push({
            class: name,
            size_old: entryA.uncompSize,
            size_new: entryB.uncompSize,
            size_diff: entryB.uncompSize - entryA.uncompSize,
          });
        }
      }
    }

    for (const name of mapA.keys()) {
      if (!mapB.has(name)) {
        removed.push(name);
      }
    }

    // Check manifest / plugin.yml
    const ymlA = entriesA.find((e) => e.name === "plugin.yml");
    const ymlB = entriesB.find((e) => e.name === "plugin.yml");
    const manifestChanged = (ymlA?.crc32 ?? 0) !== (ymlB?.crc32 ?? 0);

    return {
      ok: true,
      diff: {
        jar_a: path.basename(jarA),
        jar_b: path.basename(jarB),
        total_classes_a: mapA.size,
        total_classes_b: mapB.size,
        added_count: added.length,
        removed_count: removed.length,
        modified_count: modified.length,
        identical_count: identicalCount,
        added: added.sort(),
        removed: removed.sort(),
        modified: modified.sort((a, b) => Math.abs(b.size_diff) - Math.abs(a.size_diff)),
        manifest_changed: manifestChanged,
      },
    };
  } catch (e: any) {
    return { ok: false, error: e?.message || String(e) };
  }
}

export async function getPluginTree(jarPath: string) {
  try {
    const entries = await listZipEntries(jarPath);
    let pluginYmlText = "";
    const pluginYmlEntry = entries.find((e) => e.name === "plugin.yml");
    if (pluginYmlEntry) {
      try {
        const buf = await readZipEntry(jarPath, pluginYmlEntry, MAX_PLUGIN_YML_BYTES);
        pluginYmlText = buf.toString("utf-8");
      } catch {
        // ignore
      }
    }

    const manifest: { name?: string; version?: string; main?: string; api_version?: string; author?: string } = {};
    const commands: Array<{ command: string; description?: string; permission?: string; aliases?: string }> = [];
    const permissions: Array<{ permission: string; description?: string; default?: string }> = [];

    // Parse plugin.yml lines
    if (pluginYmlText) {
      const lines = pluginYmlText.split(/\r?\n/);
      let currentSection: "root" | "commands" | "permissions" = "root";
      let currentItem: any = null;

      for (let line of lines) {
        const raw = line;
        line = line.trimEnd();
        if (!line || line.trim().startsWith("#")) continue;

        const indent = raw.length - raw.trimStart().length;
        const trimmed = raw.trim();

        if (indent === 0) {
          if (trimmed.startsWith("name:")) manifest.name = trimmed.slice(5).trim().replace(/^['"]|['"]$/g, "");
          else if (trimmed.startsWith("version:")) manifest.version = trimmed.slice(8).trim().replace(/^['"]|['"]$/g, "");
          else if (trimmed.startsWith("main:")) manifest.main = trimmed.slice(5).trim().replace(/^['"]|['"]$/g, "");
          else if (trimmed.startsWith("api-version:")) manifest.api_version = trimmed.slice(12).trim().replace(/^['"]|['"]$/g, "");
          else if (trimmed.startsWith("author:")) manifest.author = trimmed.slice(7).trim().replace(/^['"]|['"]$/g, "");
          else if (trimmed.startsWith("commands:")) currentSection = "commands";
          else if (trimmed.startsWith("permissions:") || trimmed.startsWith("permission:")) currentSection = "permissions";
          else currentSection = "root";
          currentItem = null;
          continue;
        }

        if (currentSection === "commands") {
          if (indent === 2 && trimmed.endsWith(":")) {
            const cmdName = trimmed.slice(0, -1).trim().replace(/^['"]|['"]$/g, "");
            currentItem = { command: `/${cmdName}` };
            commands.push(currentItem);
          } else if (currentItem && indent >= 4) {
            if (trimmed.startsWith("description:")) currentItem.description = trimmed.slice(12).trim().replace(/^['"]|['"]$/g, "");
            else if (trimmed.startsWith("permission:")) currentItem.permission = trimmed.slice(11).trim().replace(/^['"]|['"]$/g, "");
            else if (trimmed.startsWith("aliases:")) currentItem.aliases = trimmed.slice(8).trim().replace(/^['"]|['"]$/g, "");
          }
        } else if (currentSection === "permissions") {
          if (indent === 2 && trimmed.endsWith(":")) {
            const permName = trimmed.slice(0, -1).trim().replace(/^['"]|['"]$/g, "");
            currentItem = { permission: permName };
            permissions.push(currentItem);
          } else if (currentItem && indent >= 4) {
            if (trimmed.startsWith("description:")) currentItem.description = trimmed.slice(12).trim().replace(/^['"]|['"]$/g, "");
            else if (trimmed.startsWith("default:")) currentItem.default = trimmed.slice(8).trim().replace(/^['"]|['"]$/g, "");
          }
        }
      }
    }

    // Find listener classes
    const listeners: string[] = [];
    for (const e of entries) {
      if (e.name.endsWith(".class")) {
        const simpleName = path.basename(e.name, ".class");
        if (
          simpleName.toLowerCase().includes("listener") ||
          simpleName.toLowerCase().includes("event") ||
          simpleName.toLowerCase().includes("handler")
        ) {
          listeners.push(e.name.replace(/\.class$/, "").replace(/\//g, "."));
        }
      }
    }

    return {
      ok: true,
      tree: {
        jar: path.basename(jarPath),
        manifest,
        commands,
        permissions,
        listeners: listeners.sort(),
      },
    };
  } catch (e: any) {
    return { ok: false, error: e?.message || String(e) };
  }
}

// ---------------------------------------------------------------------------
// GitHub export
// ---------------------------------------------------------------------------

const GITHUB_API_TIMEOUT_MS = 30000;
const GITHUB_API_MAX_BODY = 1024 * 1024;

type GitHubApiResponse = { ok: boolean; status: number; body: string };

function githubCreateRepo(token: string, postData: string): Promise<GitHubApiResponse> {
  return new Promise((resolve, reject) => {
    const req = https.request(
      "https://api.github.com/user/repos",
      {
        method: "POST",
        headers: {
          "User-Agent": "NanoDecompiler",
          Authorization: `Bearer ${token}`,
          Accept: "application/vnd.github.v3+json",
          "Content-Type": "application/json",
          "Content-Length": Buffer.byteLength(postData),
        },
      },
      (res) => {
        res.setEncoding("utf8");
        let body = "";
        res.on("data", (chunk: string) => {
          body += chunk;
          if (body.length > GITHUB_API_MAX_BODY) {
            req.destroy(new Error("ответ GitHub слишком большой"));
          }
        });
        res.on("end", () => resolve({ ok: (res.statusCode ?? 500) < 300, status: res.statusCode ?? 500, body }));
        res.on("error", reject);
      },
    );
    req.on("error", reject);
    req.setTimeout(GITHUB_API_TIMEOUT_MS, () => req.destroy(new Error("таймаут запроса к GitHub")));
    req.write(postData);
    req.end();
  });
}

function expandHome(p: string): string {
  if (p === "~") return os.homedir();
  if (p.startsWith("~/") || p.startsWith("~\\")) return path.join(os.homedir(), p.slice(2));
  return p;
}

export async function exportToGitHub(options: {
  outDir: string;
  repoName: string;
  token: string;
  isPrivate: boolean;
}): Promise<{ ok: boolean; url?: string; error?: string }> {
  const raw = (options ?? {}) as Partial<typeof options>;
  const token = typeof raw.token === "string" ? raw.token.trim() : "";
  const repoName = typeof raw.repoName === "string" ? raw.repoName.trim() : "";
  const isPrivate = raw.isPrivate === true;

  // Любое сообщение, которое уходит в UI/логи, обязано пройти через redact():
  // execFile кладёт в err.message ПОЛНУЮ командную строку и stderr git, а
  // stderr/ошибки сети могут содержать токен (раньше он ещё и сидел в argv).
  const authHeader = token ? `Basic ${Buffer.from(`x-access-token:${token}`).toString("base64")}` : "";
  const redact = (s: string): string => {
    let out = s;
    if (token) out = out.split(token).join("***");
    if (authHeader) out = out.split(authHeader).join("***");
    return out;
  };

  if (!/^[A-Za-z0-9_\-]{20,255}$/.test(token)) {
    return { ok: false, error: "Некорректный формат GitHub токена" };
  }
  if (!/^[A-Za-z0-9._-]{1,100}$/.test(repoName) || repoName === "." || repoName === "..") {
    return { ok: false, error: "Имя репозитория: 1-100 символов из [A-Za-z0-9._-]" };
  }
  if (typeof raw.outDir !== "string" || !raw.outDir.trim() || raw.outDir.includes("\0")) {
    return { ok: false, error: "Не указана директория проекта" };
  }
  // outDir хранится в состоянии GUI с буквальной "~" (настройка outputDir по
  // умолчанию "~/NanoDecompiler/out") - fs тильду не разворачивает, раньше
  // экспорт для такого пути всегда падал с "Директория не существует".
  const outDir = path.resolve(expandHome(raw.outDir.trim()));
  let stat: fs.Stats;
  try {
    stat = fs.statSync(outDir);
  } catch {
    return { ok: false, error: `Директория ${outDir} не существует` };
  }
  if (!stat.isDirectory()) {
    return { ok: false, error: `${outDir} не является директорией` };
  }
  if (path.parse(outDir).root === outDir || outDir === os.homedir()) {
    return { ok: false, error: "Нельзя экспортировать корень диска или домашнюю папку целиком" };
  }

  const postData = JSON.stringify({
    name: repoName,
    private: isPrivate,
    auto_init: false,
    description: "Decompiled with NanoDecompiler 2.0.0-alpha",
  });

  const baseEnv = { ...process.env, GIT_TERMINAL_PROMPT: "0" };
  const git = (args: string[], env: NodeJS.ProcessEnv = baseEnv, timeout = 120000) =>
    execFileAsync("git", args, { cwd: outDir, env, timeout, windowsHide: true, maxBuffer: 16 * 1024 * 1024 });

  let htmlUrl = "";
  try {
    const createRes = await githubCreateRepo(token, postData);

    let repoJson: any = {};
    try {
      repoJson = JSON.parse(createRes.body);
    } catch {
      // ignore
    }

    if (!createRes.ok) {
      const msg = repoJson?.message || `Ошибка создания репозитория HTTP ${createRes.status}`;
      return { ok: false, error: redact(String(msg)) };
    }

    htmlUrl = typeof repoJson.html_url === "string" ? repoJson.html_url : "";
    const cloneUrl = typeof repoJson.clone_url === "string" ? repoJson.clone_url : "";

    if (!cloneUrl || !/^https:\/\/github\.com\//i.test(cloneUrl)) {
      return { ok: false, error: "GitHub не вернул корректный URL репозитория" };
    }

    // Токен НЕ попадает ни в URL remote, ни в argv git (его видно в списке
    // процессов и в .git/config): авторизация передаётся ТОЛЬКО через
    // переменные окружения дочернего процесса (GIT_CONFIG_COUNT/KEY/VALUE,
    // git >= 2.31) и только на время push.
    const pushEnv: NodeJS.ProcessEnv = {
      ...baseEnv,
      GIT_CONFIG_COUNT: "1",
      GIT_CONFIG_KEY_0: "http.extraHeader",
      GIT_CONFIG_VALUE_0: `Authorization: ${authHeader}`,
    };

    await git(["init"]);
    await git(["config", "user.name", "NanoDecompiler"]);
    await git(["config", "user.email", "nanodecompiler@local"]);
    await git(["add", "."]);

    try {
      await git(["commit", "-m", "Decompiled with NanoDecompiler v2.0.0-alpha"]);
    } catch (e: any) {
      // "nothing to commit" (повторный экспорт того же дерева) - не ошибка,
      // любые другие причины (нет identity, сбой git) раньше глотались и
      // превращались в загадочное "src refspec main does not match any".
      const text = `${e?.stdout ?? ""}${e?.stderr ?? ""}${e?.message ?? ""}`;
      if (!/nothing to commit|nothing added to commit|no changes added/i.test(text)) throw e;
    }

    await git(["branch", "-M", "main"]);

    try {
      await git(["remote", "remove", "origin"]);
    } catch {
      // Ignore if origin doesn't exist
    }

    await git(["remote", "add", "origin", cloneUrl]);
    // Репозиторий только что создан и пуст - --force не нужен (раньше push
    // шёл с --force и мог затереть чужую историю, если создание повторялось).
    await git(["push", "-u", "origin", "main"], pushEnv, 600000);

    return { ok: true, url: htmlUrl };
  } catch (err: any) {
    const base = redact(String(err?.stderr || err?.message || err));
    const hint = htmlUrl ? ` (репозиторий уже создан: ${htmlUrl} - удалите его вручную или повторите отправку)` : "";
    return { ok: false, error: base + hint };
  }
}


function computeCrc32(buf: Buffer): number {
  let crc = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    crc ^= buf[i];
    for (let j = 0; j < 8; j++) {
      crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function createSingleFileZip(name: string, data: Buffer): Buffer {
  const nameBuf = Buffer.from(name.replace(/\\/g, "/"), "utf-8");
  const crc = computeCrc32(data);
  const lh = Buffer.alloc(30);
  lh.writeUInt32LE(0x04034b50, 0);
  lh.writeUInt16LE(20, 4);
  lh.writeUInt16LE(0, 6);
  lh.writeUInt16LE(0, 8);
  lh.writeUInt16LE(0, 10);
  lh.writeUInt16LE(0, 12);
  lh.writeUInt32LE(crc, 14);
  lh.writeUInt32LE(data.length, 18);
  lh.writeUInt32LE(data.length, 22);
  lh.writeUInt16LE(nameBuf.length, 26);
  lh.writeUInt16LE(0, 28);

  const cdh = Buffer.alloc(46);
  cdh.writeUInt32LE(0x02014b50, 0);
  cdh.writeUInt16LE(20, 4);
  cdh.writeUInt16LE(20, 6);
  cdh.writeUInt16LE(0, 8);
  cdh.writeUInt16LE(0, 10);
  cdh.writeUInt16LE(0, 12);
  cdh.writeUInt16LE(0, 14);
  cdh.writeUInt32LE(crc, 16);
  cdh.writeUInt32LE(data.length, 20);
  cdh.writeUInt32LE(data.length, 24);
  cdh.writeUInt16LE(nameBuf.length, 28);
  cdh.writeUInt16LE(0, 30);
  cdh.writeUInt16LE(0, 32);
  cdh.writeUInt16LE(0, 34);
  cdh.writeUInt16LE(0, 36);
  cdh.writeUInt32LE(0, 38);
  cdh.writeUInt32LE(0, 42);

  const cdOffset = lh.length + nameBuf.length + data.length;
  const cdSize = cdh.length + nameBuf.length;

  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(0, 4);
  eocd.writeUInt16LE(0, 6);
  eocd.writeUInt16LE(1, 8);
  eocd.writeUInt16LE(1, 10);
  eocd.writeUInt32LE(cdSize, 12);
  eocd.writeUInt32LE(cdOffset, 16);
  eocd.writeUInt16LE(0, 20);

  return Buffer.concat([lh, nameBuf, data, cdh, nameBuf, eocd]);
}

export function computeLineDiff(linesA: string[], linesB: string[]) {
  const n = linesA.length;
  const m = linesB.length;
  if (n * m <= 2500000) {
    const dp: number[][] = Array.from({ length: n + 1 }, () => new Int32Array(m + 1) as any);
    for (let i = 0; i < n; i++) {
      for (let j = 0; j < m; j++) {
        if (linesA[i] === linesB[j]) {
          dp[i + 1][j + 1] = dp[i][j] + 1;
        } else {
          dp[i + 1][j + 1] = Math.max(dp[i + 1][j], dp[i][j + 1]);
        }
      }
    }
    const diff: Array<{ type: "same" | "add" | "remove"; text: string; lineA?: number; lineB?: number }> = [];
    let i = n, j = m;
    while (i > 0 || j > 0) {
      if (i > 0 && j > 0 && linesA[i - 1] === linesB[j - 1]) {
        diff.push({ type: "same", text: linesA[i - 1], lineA: i, lineB: j });
        i--;
        j--;
      } else if (j > 0 && (i === 0 || dp[i][j - 1] >= dp[i - 1][j])) {
        diff.push({ type: "add", text: linesB[j - 1], lineB: j });
        j--;
      } else if (i > 0 && (j === 0 || dp[i][j - 1] < dp[i - 1][j])) {
        diff.push({ type: "remove", text: linesA[i - 1], lineA: i });
        i--;
      }
    }
    return diff.reverse();
  }

  const diff: Array<{ type: "same" | "add" | "remove"; text: string; lineA?: number; lineB?: number }> = [];
  const max = Math.max(n, m);
  for (let k = 0; k < max; k++) {
    if (k < n && k < m) {
      if (linesA[k] === linesB[k]) {
        diff.push({ type: "same", text: linesA[k], lineA: k + 1, lineB: k + 1 });
      } else {
        diff.push({ type: "remove", text: linesA[k], lineA: k + 1 });
        diff.push({ type: "add", text: linesB[k], lineB: k + 1 });
      }
    } else if (k < n) {
      diff.push({ type: "remove", text: linesA[k], lineA: k + 1 });
    } else if (k < m) {
      diff.push({ type: "add", text: linesB[k], lineB: k + 1 });
    }
  }
  return diff;
}

async function decompileClassBytecode(
  classRelPath: string,
  bytecode: Buffer,
  engineBin: string,
  tempPrefix: string
): Promise<string> {
  const tmpDir = path.join(os.tmpdir(), `nd_diff_${tempPrefix}_${Date.now()}_${Math.random().toString(36).slice(2, 6)}`);
  const jarFile = path.join(tmpDir, "single.jar");
  const outDir = path.join(tmpDir, "out");
  await fs.promises.mkdir(tmpDir, { recursive: true });
  try {
    const jarBuf = createSingleFileZip(classRelPath, bytecode);
    await fs.promises.writeFile(jarFile, jarBuf);
    await execFileAsync(engineBin, [jarFile, outDir, "--no-legitimacy-check", "--json-output"], { timeout: 20000 });
    const javaRel = classRelPath.replace(/\.class$/i, ".java");
    const candidatePath = path.join(outDir, "src", "main", "java", javaRel);
    if (fs.existsSync(candidatePath)) {
      return await fs.promises.readFile(candidatePath, "utf-8");
    }
    const findJava = async (dir: string): Promise<string | null> => {
      if (!fs.existsSync(dir)) return null;
      const entries = await fs.promises.readdir(dir, { withFileTypes: true });
      for (const e of entries) {
        const full = path.join(dir, e.name);
        if (e.isDirectory()) {
          const res = await findJava(full);
          if (res) return res;
        } else if (e.isFile() && e.name.endsWith(".java")) {
          return await fs.promises.readFile(full, "utf-8");
        }
      }
      return null;
    };
    const found = await findJava(outDir);
    return found || "// Не удалось декомпилировать байткод класса";
  } finally {
    try {
      await fs.promises.rm(tmpDir, { recursive: true, force: true });
    } catch {}
  }
}

export async function diffClassBetweenJars(
  jarA: string,
  jarB: string,
  classRelPath: string,
  engineBin: string
) {
  try {
    const [entriesA, entriesB] = await Promise.all([listZipEntries(jarA), listZipEntries(jarB)]);
    const entryA = entriesA.find(e => e.name === classRelPath);
    const entryB = entriesB.find(e => e.name === classRelPath);

    let codeA = "";
    let codeB = "";

    if (entryA) {
      const bufA = await readZipEntry(jarA, entryA);
      codeA = await decompileClassBytecode(classRelPath, bufA, engineBin, "a");
    }
    if (entryB) {
      const bufB = await readZipEntry(jarB, entryB);
      codeB = await decompileClassBytecode(classRelPath, bufB, engineBin, "b");
    }

    const linesA = codeA ? codeA.split("\n") : [];
    const linesB = codeB ? codeB.split("\n") : [];
    const diffLines = computeLineDiff(linesA, linesB);

    return {
      ok: true,
      className: classRelPath,
      codeA,
      codeB,
      diffLines,
    };
  } catch (e: any) {
    return { ok: false, error: e?.message || String(e) };
  }
}
