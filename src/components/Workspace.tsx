import { FileArchive, Play, TriangleAlert } from "lucide-react";
import { useMemo, type ReactNode } from "react";
import { useEngine } from "../state/engine";
import { FileTree } from "./FileTree";
import { CodeView } from "./CodeView";
import { fmtBytes } from "../lib/model";
import { t } from "../lib/i18n";

function Centered({ children }: { children: ReactNode }) {
  return (
    <div className="grid flex-1 place-items-center overflow-y-auto bg-bg p-6">
      <div className="w-full max-w-[540px] rounded-2xl border border-dashed border-line-strong bg-surface/60 px-8 py-9">
        {children}
      </div>
    </div>
  );
}

// БАГ-ФИКС: тот же ложный текст "движку нужен JRE 17+", что уже был
// исправлен в StatusBar/SettingsModal - здесь про него забыли. Движок на
// чистом C++, Java для декомпиляции не нужна вообще, это никогда не
// блокирует работу - баннер занижен по тревожности и текст честный.
function EnvBanner() {
  const { resolveEnvIssue, setSettingsOpen, settings } = useEngine();
  const lang = settings.language;
  return (
    <div className="flex flex-none items-center gap-3 border-b border-line bg-raised/40 px-4 py-2">
      <TriangleAlert size={14} className="flex-none text-warn/70" />
      <div className="min-w-0 flex-1">
        <p className="text-[12px] text-dim">
          {lang === "ru"
            ? "Java не найдена — это не мешает декомпиляции, нужна только для ручной сборки (mvn compile) результата."
            : "Java not found — this does not prevent decompilation, only needed for manual build (mvn compile)."}
        </p>
      </div>
      <button className="btn btn-ghost h-6 text-[11px]" onClick={resolveEnvIssue}>
        {t(lang, "settings.recheck")}
      </button>
      <button className="btn btn-ghost h-6 text-[11px]" onClick={() => setSettingsOpen(true)}>
        {t(lang, "app.settings")}
      </button>
    </div>
  );
}

export function Workspace() {
  const { jobs, selectedJob, openFileByJob, selectFile, startQueue, openFileDialog, log, envIssue, settings } =
    useEngine();
  const lang = settings.language;

  const lastPhase = useMemo(() => {
    if (!selectedJob) return null;
    for (let i = log.length - 1; i >= 0; i--) {
      if (log[i].jobId === selectedJob.id && log[i].tag !== "engine") return log[i].msg;
    }
    return null;
  }, [log, selectedJob]);

  const pendingIds = useMemo(
    () => jobs.filter(j => j.status === "queued" || j.status === "canceled" || j.status === "failed").map(j => j.id),
    [jobs],
  );

  /* готово → дерево + код */
  if (selectedJob?.status === "done" && selectedJob.files) {
    const openId = openFileByJob[selectedJob.id];
    const file = selectedJob.files.find(f => f.id === openId) ?? selectedJob.files[0] ?? null;
    return (
      <div className="flex min-h-0 flex-1 flex-col">
        {envIssue && <EnvBanner />}
        <div className="flex min-h-0 flex-1">
          <FileTree
            files={selectedJob.files}
            openId={file?.id}
            onSelect={fid => selectFile(selectedJob.id, fid)}
            totalSourceLines={selectedJob.details?.stats.total_source_lines}
          />
          <CodeView file={file} jobId={selectedJob.id} outDir={selectedJob.outDir} />
        </div>
      </div>
    );
  }

  /* идёт декомпиляция */
  if (selectedJob?.status === "running") {
    const pct = Math.round(selectedJob.progress * 100);
    return (
      <div className="flex min-h-0 flex-1 flex-col">
        {envIssue && <EnvBanner />}
              <Centered>
        {/* Квантовые светящиеся сферы Liquid Glass */}
        <div className="absolute inset-0 pointer-events-none overflow-hidden flex items-center justify-center">
          <div className="absolute w-3.5 h-3.5 rounded-full bg-acid orb-vivid-1" style={{ top: '22%', left: '28%' }} />
          <div className="absolute w-4 h-4 rounded-full bg-sky-400 orb-vivid-2" style={{ top: '68%', right: '26%' }} />
          <div className="absolute w-3 h-3 rounded-full bg-purple-500 orb-vivid-3" style={{ bottom: '18%', left: '38%' }} />
        </div>

        {/* Интерактивная карточка в стиле Liquid Glass + MD3 */}
        <div className="relative liquid-glass md3-card p-8 flex flex-col items-center max-w-md text-center">
          <div className="relative flex h-16 w-16 items-center justify-center rounded-2xl border border-white/20 bg-black/40 shadow-[0_0_35px_rgba(155,242,70,0.3)] mb-4">
            <FileArchive size={28} className="text-acid relative z-10" />
            <div className="absolute -bottom-2 flex items-center gap-1.5">
              <span className="w-1.5 rounded-full bg-acid dna-bar-1" />
              <span className="w-1.5 rounded-full bg-acid dna-bar-2" />
              <span className="w-1.5 rounded-full bg-acid dna-bar-3" />
              <span className="w-1.5 rounded-full bg-acid dna-bar-4" />
            </div>
          </div>

          <p className="text-lg font-bold text-ink tracking-tight">
            {jobs.length === 0
              ? (lang === "ru" ? "Пока нечего декомпилировать." : "Nothing to decompile yet.")
              : (lang === "ru" ? "Задача не выбрана." : "No job selected.")}
          </p>
          <p className="mt-2 text-xs leading-relaxed text-dim max-w-sm">
            {lang === "ru"
              ? "Перетащите .jar плагин или архив в левую панель. Движок распакует файлы, восстановит Java-код и отобразит дерево классов."
              : "Drag and drop any .jar plugin into the left panel to decompile."}
          </p>
          <div className="mt-5">
            <button className="btn btn-acid md3-pill h-10 px-6 text-xs font-semibold shadow-[0_0_24px_rgba(155,242,70,0.35)] flex items-center gap-2" onClick={openFileDialog}>
              <FileArchive size={15} />
              {t(lang, "workspace.select_jar")}
            </button>
          </div>
        </div>
      </Centered>
      </div>
    );
  }

  /* в очереди / остановлено / ошибка */
  if (selectedJob) {
    const position = pendingIds.indexOf(selectedJob.id) + 1;
    const isNext = pendingIds[0] === selectedJob.id;
    return (
      <div className="flex min-h-0 flex-1 flex-col">
        {envIssue && <EnvBanner />}
        <Centered>
          <span className="kicker">
            {selectedJob.status === "failed"
              ? (lang === "ru" ? "Ошибка" : "Error")
              : selectedJob.status === "canceled"
                ? (lang === "ru" ? "Остановлено" : "Stopped")
                : (lang === "ru" ? "В очереди" : "In Queue")}
          </span>
          <p className="mono mt-3 text-[15px] font-semibold text-ink">{selectedJob.fileName}</p>
          <p className="mono mt-1.5 text-[11.5px] text-faint">
            {fmtBytes(selectedJob.sizeBytes)}
            {selectedJob.status === "queued" && position > 0 && ` · ${lang === "ru" ? `позиция ${position}` : `position ${position}`}`}
            {selectedJob.status === "canceled" && (lang === "ru" ? " · прогресс не сохранён" : " · progress not saved")}
            {selectedJob.status === "failed" && (selectedJob.error ? ` · ${selectedJob.error}` : "")}
          </p>
          <div className="mt-5 flex items-center gap-2">
            <button className="btn btn-acid" onClick={startQueue}>
              <Play size={14} />
              {isNext ? t(lang, "app.start") : pendingIds.length > 1 ? (lang === "ru" ? "Запустить очередь" : "Start queue") : t(lang, "app.start")}
            </button>
            {selectedJob.classCount === null && (
              <span className="mono text-[10.5px] text-faint">
                {lang === "ru" ? "архив будет просканирован при запуске" : "archive will be scanned on start"}
              </span>
            )}
          </div>
        </Centered>
      </div>
    );
  }

  /* пустое состояние */
  return (
    <div className="flex min-h-0 flex-1 flex-col">
      {envIssue && <EnvBanner />}
      <Centered>
        <div className="flex size-10 items-center justify-center rounded-xl border border-line bg-raised">
          <FileArchive size={18} className="text-dim" />
        </div>
        <p className="mt-4 text-[15px] font-semibold text-ink">
          {jobs.length === 0
            ? (lang === "ru" ? "Пока нечего декомпилировать." : "Nothing to decompile yet.")
            : (lang === "ru" ? "Задача не выбрана." : "No job selected.")}
        </p>
        <p className="mt-2 max-w-[420px] text-[12.5px] leading-relaxed text-dim">
          {lang === "ru"
            ? "Добавьте .jar слева. Движок распакует архив, восстановит .java и покажет дерево классов на этом месте."
            : "Add a .jar on the left. The engine will unpack the archive, restore .java, and show class tree here."}
        </p>
        <div className="mt-5">
          <button className="btn btn-tonal" onClick={openFileDialog}>
            <FileArchive size={14} />
            {t(lang, "workspace.select_jar")}
          </button>
        </div>
      </Centered>
    </div>
  );
}
