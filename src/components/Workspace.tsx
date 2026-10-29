import { FileArchive, Play, TriangleAlert, Loader2, Sparkles } from "lucide-react";
import { useMemo, type ReactNode } from "react";
import { useEngine } from "../state/engine";
import { FileTree } from "./FileTree";
import { CodeView } from "./CodeView";
import { fmtBytes } from "../lib/model";
import { t } from "../lib/i18n";

function Centered({ children }: { children: ReactNode }) {
  return (
    <div className="relative flex-1 grid place-items-center overflow-y-auto bg-bg p-3 md:p-6 overflow-hidden w-full">
      {/* Квантовые светящиеся сферы Liquid Glass */}
      <div className="absolute inset-0 pointer-events-none overflow-hidden flex items-center justify-center z-0">
        <div className="absolute w-4 h-4 rounded-full bg-acid orb-vivid-1" style={{ top: '22%', left: '16%' }} />
        <div className="absolute w-5 h-5 rounded-full bg-sky-400 orb-vivid-2" style={{ top: '68%', right: '14%' }} />
        <div className="absolute w-4 h-4 rounded-full bg-purple-500 orb-vivid-3" style={{ bottom: '16%', left: '26%' }} />
      </div>
      <div className="relative z-10 w-full max-w-[420px] flex justify-center">
        {children}
      </div>
    </div>
  );
}

function EnvBanner() {
  const { resolveEnvIssue, setSettingsOpen, settings } = useEngine();
  const lang = settings.language;
  return (
    <div className="flex flex-none items-center gap-3 border-b border-line bg-raised/40 px-4 py-2 relative z-10">
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
        <div className="flex min-h-0 flex-1 flex-col md:flex-row">
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
          <div className="liquid-glass md3-card p-8 flex flex-col items-center text-center shadow-[0_20px_60px_rgba(0,0,0,0.8)]">
            {/* Анимированный реактор/спиннер */}
            <div className="relative flex h-18 w-18 items-center justify-center rounded-2xl border border-acid/30 bg-acid/10 shadow-[0_0_35px_rgba(155,242,70,0.35)] mb-4">
              <Loader2 size={32} className="animate-spin text-acid" />
              <div className="absolute -bottom-2 flex items-center gap-1.5">
                <span className="w-1.5 rounded-full bg-acid dna-bar-1" />
                <span className="w-1.5 rounded-full bg-acid dna-bar-2" />
                <span className="w-1.5 rounded-full bg-acid dna-bar-3" />
                <span className="w-1.5 rounded-full bg-acid dna-bar-4" />
              </div>
            </div>

            <p className="text-base font-bold text-ink tracking-tight mono">
              {selectedJob.fileName}
            </p>

            <p className="mt-1 text-xs text-acid font-medium mono animate-pulse">
              {lastPhase || (lang === "ru" ? "Декомпиляция классов и ресурсов..." : "Decompiling bytecode...")}
            </p>

            {/* Прогресс-бар Liquid Glass */}
            <div className="mt-5 w-full bg-black/50 border border-white/10 rounded-full h-3 overflow-hidden p-0.5 shadow-inner">
              <div
                className="h-full bg-gradient-to-r from-acid via-emerald-400 to-acid rounded-full transition-all duration-300 shadow-[0_0_16px_rgba(155,242,70,0.8)]"
                style={{ width: `${Math.max(pct, 5)}%` }}
              />
            </div>

            <div className="mt-2.5 flex items-center justify-between w-full mono text-2xs text-dim">
              <span>{pct}% {lang === "ru" ? "завершено" : "complete"}</span>
              {selectedJob.classCount ? (
                <span>{selectedJob.classCount} {lang === "ru" ? "классов" : "classes"}</span>
              ) : (
                <span>{fmtBytes(selectedJob.sizeBytes)}</span>
              )}
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
          <div className="liquid-glass md3-card p-6 md:p-8 w-full max-w-[360px] flex flex-col items-center text-center">
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
              <button className="btn btn-acid md3-pill px-6 shadow-[0_0_20px_rgba(155,242,70,0.35)]" onClick={startQueue}>
                <Play size={14} />
                {isNext ? t(lang, "app.start") : pendingIds.length > 1 ? (lang === "ru" ? "Запустить очередь" : "Start queue") : t(lang, "app.start")}
              </button>
            </div>
          </div>
        </Centered>
      </div>
    );
  }

  /* пустое начальное состояние: выразительный Liquid Glass + MD3 */
  return (
    <div className="flex min-h-0 flex-1 flex-col">
      {envIssue && <EnvBanner />}
      <Centered>
        <div className="liquid-glass md3-card p-6 md:p-9 w-full max-w-[360px] flex flex-col items-center text-center shadow-[0_25px_60px_rgba(0,0,0,0.85)]">
          {/* Анимированная иконка архива со световой аурой и ДНК-волной */}
          <div className="relative flex h-18 w-18 items-center justify-center rounded-2xl border border-white/20 bg-black/40 shadow-[0_0_40px_rgba(155,242,70,0.35)] mb-4">
            <FileArchive size={30} className="text-acid relative z-10" />
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
              ? "Перетащите .jar плагин или архив в левую панель. Движок распакует файлы, восстановит исходный Java-код и отобразит дерево классов."
              : "Drag and drop any .jar plugin into the left panel to decompile."}
          </p>

          <div className="mt-6 flex items-center gap-3">
            <button
              className="btn btn-acid md3-pill h-10 px-6 text-xs font-semibold shadow-[0_0_25px_rgba(155,242,70,0.4)] flex items-center gap-2 hover:scale-105 active:scale-95 transition-transform"
              onClick={openFileDialog}
            >
              <Sparkles size={15} />
              {t(lang, "workspace.select_jar")}
            </button>
          </div>
        </div>
      </Centered>
    </div>
  );
}
