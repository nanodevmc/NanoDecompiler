import * as fs from "fs";
import * as path from "path";
import { listZipEntries, readZipEntry, type ZipEntry } from "./zipReader";

/**
 * Читает сводку по .jar (размер/классы/пакеты/версия Java/имя плагина)
 * напрямую через central directory ZIP-формата, БЕЗ спавна подпроцесса
 * движка. См. HANDOFF_22/46: раньше каждый выбор jar в UI спавнил
 * `python3 main.py --jar-summary ...` (или собранный .exe) - для
 * PyInstaller onefile-сборки это ещё и самораспаковка exe во временную
 * папку НА КАЖДЫЙ вызов (известная особенность --onefile) - десятки-
 * сотни миллисекунд, ощущавшиеся как "очень долго определяется размер".
 * Движок теперь на C++ (cli_main.cpp --jar-summary, HANDOFF_46) - этой
 * проблемы с самораспаковкой больше нет, но читать central directory
 * напрямую в Node всё равно быстрее (без spawn() вообще) - оставлено как
 * основной путь. Здесь читаются только МЕТАДАННЫЕ central directory (имена
 * файлов, размеры, смещения) - без распаковки содержимого, КРОМЕ двух
 * маленьких файлов (первый .class - 8 байт заголовка для версии Java,
 * plugin.yml целиком - обычно первые сотни байт).
 *
 * БАГ-ФИКС v1.7.3.1 (реальная жалоба - "информация о плагине составляется
 * до 10 секунд, похоже на медленную разархивацию"): САМ алгоритм здесь
 * никогда не был медленным - замерено вживую на реальном ViaVersion.jar
 * (3352 класса, 6МБ) - около 400мс. Проблема была в другом: все функции
 * использовали `fs.readSync`/`fs.openSync` - СИНХРОННЫЙ, блокирующий I/O.
 * Electron main-процесс ОДНОПОТОЧНЫЙ - пока идёт синхронное чтение файла,
 * ЛЮБОЕ другое событие (перерисовка окна, другой IPC-вызов) ждёт. Когда
 * пользователь добавляет НЕСКОЛЬКО jar разом, engine.tsx запускает
 * jarSummary() для каждого "параллельно" СО СТОРОНЫ РЕНДЕРЕРА, но на
 * стороне main-процесса они всё равно выполняются ПОДРЯД, каждый вызов
 * блокируя ВЕСЬ интерфейс приложения на своё время - для десятка jar это
 * складывается в те самые "десять секунд", когда всё натурально
 * зависает (не только эта карточка - вообще всё окно). Переписано на
 * `fs.promises` (`fs/promises`) - та же логика, та же скорость на один
 * файл, но БЕЗ блокировки event loop - несколько jar теперь реально
 * обрабатываются с чередованием, а не одно окно зависания на всех.
 *
 * ZIP64 (jar'ы больше 4ГБ или с 65535+ записей - на практике НЕ
 * встречается у Bukkit-плагинов) сознательно не поддержан - при
 * малейшем подозрении (сигнатуры 0xffffffff/0xffff в EOCD) бросаем
 * исключение, вызывающая сторона (main.ts) откатывается на старый путь
 * через подпроцесс движка (jar_summary.hpp/.cpp, см. HANDOFF_46) -
 * корректность важнее скорости на редком edge-case.
 *
 * Само чтение ZIP вынесено в zipReader.ts (общий код с pluginTools.ts и
 * archiveExtractor.ts): там же проверки границ и лимиты на размер, потому
 * что все числа в заголовках контролирует автор (недоверенного) архива.
 */

export type JarSummary = {
  name: string;
  size: string;
  sizeBytes: number;
  classes: number;
  packages: number;
  java: string;
  plugin_name: string | null;
  // НОВОЕ v1.7.6: для поиска похожих репозиториев на GitHub.
  plugin_author: string | null;
};

/** Та же формула, что и java_version_from_major() в jar_summary.cpp -
 * держим в одном месте только концептуально (два языка, увы, дублировать
 * неизбежно), но проверено на точное совпадение вывода для major 45-68. */
function javaVersionFromMajor(major: number): string {
  if (major < 45) return "?";
  return major <= 48 ? `Java 1.${major - 44}` : `Java ${major - 44}`;
}

function formatSize(bytes: number): string {
  return bytes >= 1024 * 1024 ? `${(bytes / 1024 / 1024).toFixed(1)} МБ` : `${(bytes / 1024).toFixed(1)} КБ`;
}

// plugin.yml - крошечный файл; всё, что больше, не читаем целиком (защита от
// zip-bomb в недоверенном jar).
const MAX_PLUGIN_YML_BYTES = 1024 * 1024;

function stripQuotes(v: string): string {
  return v.trim().replace(/^['"]|['"]$/g, "");
}

export async function readJarSummaryNative(jarPath: string): Promise<JarSummary> {
  const fileSize = (await fs.promises.stat(jarPath)).size;
  if (jarPath.toLowerCase().endsWith(".class")) {
    const fh = await fs.promises.open(jarPath, "r");
    try {
      const header = Buffer.alloc(8);
      const { bytesRead } = await fh.read(header, 0, 8, 0);
      let java = "?";
      if (bytesRead >= 8 && header.readUInt32BE(0) === 0xcafebabe) {
        java = javaVersionFromMajor(header.readUInt16BE(6));
      }
      return {
        name: path.basename(jarPath),
        size: formatSize(fileSize),
        sizeBytes: fileSize,
        classes: 1,
        packages: 1,
        java,
        plugin_name: path.basename(jarPath, ".class"),
        plugin_author: null,
      };
    } finally {
      await fh.close();
    }
  }

  const entries: ZipEntry[] = await listZipEntries(jarPath);

  const classEntries = entries.filter((e) => e.name.endsWith(".class") && !e.name.includes("module-info"));
  const packages = new Set<string>();
  for (const e of classEntries) {
    const i = e.name.lastIndexOf("/");
    if (i >= 0) packages.add(e.name.slice(0, i));
  }

  let java = "?";
  if (classEntries.length > 0) {
    try {
      const data = await readZipEntry(jarPath, classEntries[0], 64 * 1024 * 1024);
      if (data.length >= 8) java = javaVersionFromMajor(data.readUInt16BE(6));
    } catch {
      // не критично - просто не покажем версию Java
    }
  }

  let pluginName: string | null = null;
  let pluginAuthor: string | null = null;
  const pluginYml = entries.find((e) => e.name === "plugin.yml");
  if (pluginYml) {
    try {
      const text = (await readZipEntry(jarPath, pluginYml, MAX_PLUGIN_YML_BYTES)).toString("utf-8");
      const lines = text.split(/\r?\n/);
      for (let i = 0; i < lines.length; i++) {
        // Ключи верхнего уровня - только строки БЕЗ отступа: раньше брали trim()
        // и ловили `name:` / `author:` из вложенных секций (commands, permissions).
        const raw = lines[i];
        if (!raw || /^\s/.test(raw)) continue;
        const t = raw.trim();
        if (pluginName === null && t.startsWith("name:")) {
          pluginName = stripQuotes(t.slice(5));
        } else if (pluginAuthor === null && t.startsWith("author:")) {
          pluginAuthor = stripQuotes(t.slice(7));
        } else if (pluginAuthor === null && t.startsWith("authors:")) {
          // НОВОЕ v1.7.6 (для поиска похожих репозиториев на GitHub -
          // нужен автор). "authors:" бывает ИЛИ инлайн-массивом
          // ("authors: [A, B]"), ИЛИ многострочным YAML-списком
          // ("authors:\n  - A\n  - B") - берём только ПЕРВОГО автора,
          // этого достаточно для поиска, не разбираем YAML целиком.
          const inline = t.slice(8).trim();
          if (inline.startsWith("[")) {
            const first = inline.replace(/^\[|\]$/g, "").split(",")[0];
            pluginAuthor = first ? stripQuotes(first) || null : null;
          } else if (!inline) {
            for (let j = i + 1; j < lines.length; j++) {
              const t2 = lines[j].trim();
              if (t2.startsWith("- ")) {
                pluginAuthor = stripQuotes(t2.slice(2));
                break;
              }
              if (t2 && !t2.startsWith("#")) break; // следующее YAML-поле - список закончился
            }
          }
        }
      }
    } catch {
      // не критично
    }
  }

  return {
    name: path.basename(jarPath),
    size: formatSize(fileSize),
    sizeBytes: fileSize,
    classes: classEntries.length,
    packages: packages.size,
    java,
    plugin_name: pluginName,
    plugin_author: pluginAuthor,
  };
}
