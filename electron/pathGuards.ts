import * as fs from "fs";
import * as os from "os";
import * as path from "path";

// Проверки путей, приходящих из рендерера по IPC. Рендерер считается
// недоверенным (в нём отображается текст из чужих плагинов), поэтому main-процесс
// не должен слепо исполнять над его путями чтение/запись/удаление/запуск.

/** true, если child == parent или лежит внутри parent (по границе сегмента пути). */
export function isSamePathOrInside(parent: string, child: string): boolean {
  const rel = path.relative(parent, child);
  if (rel === "") return true;
  return rel !== ".." && !rel.startsWith(".." + path.sep) && !path.isAbsolute(rel);
}

/** Корень диска или домашняя папка целиком - такие каталоги нельзя брать корнем проекта. */
export function isFilesystemRootOrHome(p: string): boolean {
  const resolved = path.resolve(p);
  return path.parse(resolved).root === resolved || resolved === path.resolve(os.homedir());
}

/**
 * Абсолютный путь `relPath` внутри `root` либо null, если он выходит за root.
 *
 * Отличия от прежней версии: (1) учитываются symlink'и - сравниваются
 * реальные пути (раньше ссылка внутри проекта на /etc или C:\Users\... проходила
 * проверку); (2) в качестве root запрещены корень диска и домашняя папка;
 * (3) NUL-байты и нестроковые аргументы отвергаются.
 */
export function resolveWithinRoot(
  root: unknown,
  relPath: unknown,
  expandHome: (p: string) => string,
): string | null {
  if (typeof root !== "string" || typeof relPath !== "string") return null;
  if (!root || root.includes("\0") || relPath.includes("\0")) return null;

  const resolvedRoot = path.resolve(expandHome(root));
  if (isFilesystemRootOrHome(resolvedRoot)) return null;
  const resolvedTarget = path.resolve(resolvedRoot, relPath || ".");
  if (!isSamePathOrInside(resolvedRoot, resolvedTarget)) return null;

  let realRoot: string;
  try {
    realRoot = fs.realpathSync(resolvedRoot);
  } catch {
    return null;
  }

  // Реальный путь ближайшего СУЩЕСТВУЮЩЕГО предка + оставшийся хвост.
  let probe = resolvedTarget;
  for (;;) {
    try {
      const realProbe = fs.realpathSync(probe);
      const finalPath = path.join(realProbe, path.relative(probe, resolvedTarget));
      if (!isSamePathOrInside(realRoot, finalPath)) return null;
      return resolvedTarget;
    } catch (e) {
      if ((e as NodeJS.ErrnoException).code !== "ENOENT") return null;
      const parent = path.dirname(probe);
      if (parent === probe) return null;
      probe = parent;
    }
  }
}

// Расширения, которые ОС выполнит при "открытии" (shell.openPath ведёт себя как
// двойной клик). Открывать такое по команде из рендерера нельзя.
const RISKY_OPEN_EXTENSIONS = new Set([
  ".exe", ".com", ".scr", ".pif", ".bat", ".cmd", ".msi", ".msp", ".msc", ".cpl", ".dll",
  ".ps1", ".psm1", ".psd1", ".vbs", ".vbe", ".js", ".jse", ".wsf", ".wsh", ".hta", ".reg",
  ".lnk", ".url", ".inf", ".gadget", ".jar", ".sh", ".command", ".app", ".appimage",
  ".desktop", ".py", ".pl", ".rb", ".run", ".bin",
]);

export function isRiskyToOpen(target: string): boolean {
  let st: fs.Stats;
  try {
    st = fs.statSync(target);
  } catch {
    return false; // не существует - shell.openPath сам вернёт ошибку
  }
  if (st.isDirectory()) {
    // .app на macOS - это каталог, но открытие его запускает программу
    return path.extname(target).toLowerCase() === ".app";
  }
  return RISKY_OPEN_EXTENSIONS.has(path.extname(target).toLowerCase());
}
