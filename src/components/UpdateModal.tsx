import { useState } from "react";
import { Check, Download, FileText, Loader2, RefreshCw, Sparkles, Terminal, X } from "lucide-react";
import { useEngine } from "../state/engine";
import { Kbd } from "./ui";
import { cn } from "../utils/cn";
import { ChangelogView } from "./ChangelogView";
import { formatVersionsDisplay } from "../lib/model";
import { ENGINE_MICRO_MODULES, EngineMicroModule } from "../lib/engineOta";

export function UpdateModal() {
  const {
    setUpdateModalOpen,
    updateInfo,
    downloadProgress,
    engineVersion,
    guiVersion,
    checkForUpdates,
    applyEngineUpdate,
    applyClientUpdate,
  } = useEngine();

  const isUpToDate = !updateInfo.checking && updateInfo.kind === "none" && !updateInfo.error;
  const hasUpdate = updateInfo.kind === "engine" || updateInfo.kind === "client";
  const formatV = (v?: string | null) => (v ? `v${v.trim().replace(/^v+/i, "")}` : "?");

  return (
    <div
      className="fixed inset-0 z-[110] grid place-items-center bg-black/75 p-4"
      onMouseDown={e => {
        if (e.target === e.currentTarget && !updateInfo.applying) setUpdateModalOpen(false);
      }}
    >
      <div
        role="dialog"
        aria-modal="true"
        aria-label="Обновления"
        className="animate-rise flex max-h-[85vh] w-[520px] flex-col overflow-hidden rounded-2xl border border-line bg-surface shadow-2xl shadow-black/60"
      >
        <div className="flex h-11 flex-none items-center gap-2 border-b border-line px-4">
          <h2 className="text-[13px] font-semibold text-ink">Центр обновлений</h2>
          <div className="flex-1" />
          <Kbd>Esc</Kbd>
          <button
            className="icon-btn h-7 w-7"
            onClick={() => setUpdateModalOpen(false)}
            aria-label="Закрыть"
            disabled={updateInfo.applying}
          >
            <X size={14} />
          </button>
        </div>

        <div className="flex flex-1 flex-col overflow-y-auto px-6 py-6 text-center">
          {/* Иконка-индикатор состояния */}
          <div className="mb-4 flex justify-center">
            <div
              className={cn(
                "grid h-16 w-16 place-items-center rounded-full border transition-all",
                updateInfo.checking
                  ? "border-line bg-bg"
                  : hasUpdate
                    ? "border-acid/50 bg-acid/10 shadow-lg shadow-acid/10"
                    : updateInfo.error
                      ? "border-err/40 bg-err/10"
                      : "border-acid/40 bg-acid/10",
              )}
            >
              {updateInfo.checking ? (
                <Loader2 size={26} className="animate-spin text-faint" />
              ) : hasUpdate ? (
                <Sparkles size={26} className="text-acid" />
              ) : updateInfo.error ? (
                <RefreshCw size={26} className="text-err" />
              ) : (
                <Check size={26} className="text-acid" />
              )}
            </div>
          </div>

          {updateInfo.checking ? (
            <p className="text-[13.5px] text-dim">Проверяю обновления на сервере…</p>
          ) : updateInfo.kind === "engine" ? (
            <div className="space-y-4">
              <div>
                <p className="text-[14px] font-semibold text-ink">Доступно обновление движка</p>
                <p className="mono mt-1 text-[12px] text-faint">
                  {engineVersion?.replace(/^NanoDecompiler /, "") ?? "?"} → {formatV(updateInfo.latestVersion)}
                </p>
              </div>

              {/* Прогресс-бар загрузки */}
              {updateInfo.applying && (
                <div className="space-y-2 rounded-xl border border-line bg-bg/60 p-3 text-left">
                  <div className="flex items-center justify-between text-[12px] font-mono text-ink">
                    <span className="flex items-center gap-1.5">
                      <Loader2 size={13} className="animate-spin text-acid" />
                      Загрузка движка...
                    </span>
                    <span className="font-semibold text-acid">
                      {downloadProgress?.percent !== null && downloadProgress?.percent !== undefined
                        ? `${downloadProgress.percent}%`
                        : "Загрузка..."}
                    </span>
                  </div>
                  <div className="h-2 w-full overflow-hidden rounded-full bg-line/60">
                    <div
                      className="h-full bg-acid transition-all duration-150 ease-out"
                      style={{
                        width: `${downloadProgress?.percent ?? (downloadProgress ? 100 : 0)}%`,
                      }}
                    />
                  </div>
                  {downloadProgress && (
                    <div className="flex justify-between text-[11px] font-mono text-faint">
                      <span>
                        {(downloadProgress.downloaded / (1024 * 1024)).toFixed(1)} MB
                        {downloadProgress.total ? ` / ${(downloadProgress.total / (1024 * 1024)).toFixed(1)} MB` : ""}
                      </span>
                      {downloadProgress.percent !== null && <span>{downloadProgress.percent}%</span>}
                    </div>
                  )}
                </div>
              )}

              <button
                className={cn(
                  "btn btn-acid h-10 w-full text-[13px] font-medium shadow-md shadow-acid/15",
                  updateInfo.applying && "pointer-events-none opacity-70",
                )}
                onClick={applyEngineUpdate}
                disabled={updateInfo.applying}
              >
                {updateInfo.applying ? <Loader2 size={14} className="animate-spin" /> : <Download size={14} />}
                {updateInfo.applying ? "Обновляю движок…" : "Обновить движок"}
              </button>
            </div>
          ) : updateInfo.kind === "client" ? (
            <div className="space-y-4">
              <div>
                <p className="text-[14px] font-semibold text-ink">Доступна новая версия приложения</p>
                <p className="mono mt-1 text-[12px] text-faint">
                  {formatV(guiVersion)} → {formatV(updateInfo.latestVersion)}
                </p>
              </div>

              {/* Прогресс-бар загрузки */}
              {updateInfo.applying && (
                <div className="space-y-2 rounded-xl border border-line bg-bg/60 p-3 text-left">
                  <div className="flex items-center justify-between text-[12px] font-mono text-ink">
                    <span className="flex items-center gap-1.5">
                      <Loader2 size={13} className="animate-spin text-acid" />
                      {updateInfo.applyingKind === "engine" ? "Загрузка движка..." : "Загрузка установщика..."}
                    </span>
                    <span className="font-semibold text-acid">
                      {downloadProgress?.percent !== null && downloadProgress?.percent !== undefined
                        ? `${downloadProgress.percent}%`
                        : "Загрузка..."}
                    </span>
                  </div>
                  <div className="h-2 w-full overflow-hidden rounded-full bg-line/60">
                    <div
                      className="h-full bg-acid transition-all duration-150 ease-out"
                      style={{
                        width: `${downloadProgress?.percent ?? (downloadProgress ? 100 : 0)}%`,
                      }}
                    />
                  </div>
                  {downloadProgress && (
                    <div className="flex justify-between text-[11px] font-mono text-faint">
                      <span>
                        {(downloadProgress.downloaded / (1024 * 1024)).toFixed(1)} MB
                        {downloadProgress.total ? ` / ${(downloadProgress.total / (1024 * 1024)).toFixed(1)} MB` : ""}
                      </span>
                      {downloadProgress.percent !== null && <span>{downloadProgress.percent}% завершено</span>}
                    </div>
                  )}
                </div>
              )}

              <div className="flex flex-col gap-2">
                <button
                  className={cn(
                    "btn btn-acid h-10 w-full text-[13px] font-medium shadow-md shadow-acid/15",
                    updateInfo.applying && "pointer-events-none opacity-70",
                  )}
                  onClick={applyClientUpdate}
                  disabled={updateInfo.applying}
                >
                  {updateInfo.applying && updateInfo.applyingKind === "client" ? (
                    <Loader2 size={14} className="animate-spin" />
                  ) : (
                    <Download size={14} />
                  )}
                  {updateInfo.applying && updateInfo.applyingKind === "client"
                    ? "Скачивание установщика…"
                    : "Скачать и обновить всё (приложение и движок)"}
                </button>

                {updateInfo.downloadUrl && (
                  <button
                    className={cn(
                      "btn btn-tonal h-9 w-full text-[12.5px] border border-line hover:border-acid/40",
                      updateInfo.applying && "pointer-events-none opacity-50 cursor-not-allowed",
                    )}
                    onClick={applyEngineUpdate}
                    disabled={updateInfo.applying}
                  >
                    {updateInfo.applying && updateInfo.applyingKind === "engine" ? (
                      <Loader2 size={13} className="animate-spin" />
                    ) : (
                      <Terminal size={13} />
                    )}
                    {updateInfo.applying && updateInfo.applyingKind === "client"
                      ? "Загрузка установщика клиента…"
                      : updateInfo.applying && updateInfo.applyingKind === "engine"
                        ? "Скачивание движка…"
                        : "Обновить только движок (CLI)"}
                  </button>
                )}
              </div>
            </div>
          ) : updateInfo.kind === "closed_beta" ? (
            <div className="space-y-4">
              <div>
                <p className="text-[14px] font-semibold text-ink">У вас закрытая Бета Версия</p>
                <p className="mono mt-1 text-[11.5px] text-faint">
                  {engineVersion?.replace(/^NanoDecompiler /, "") ?? formatV(guiVersion)} — новее последнего
                  опубликованного релиза ({formatV(updateInfo.latestVersion)})
                </p>
              </div>

              {updateInfo.downloadUrl && (
                <button
                  className={cn(
                    "btn btn-tonal h-9 w-full text-[12.5px]",
                    updateInfo.applying && "pointer-events-none opacity-70",
                  )}
                  onClick={applyEngineUpdate}
                  disabled={updateInfo.applying}
                >
                  <Terminal size={13} />
                  Переустановить последний релиз движка (CLI)
                </button>
              )}

              <button className="btn btn-ghost h-9 w-full text-[12.5px]" onClick={() => checkForUpdates()}>
                <RefreshCw size={13} />
                Проверить ещё раз
              </button>
            </div>
          ) : updateInfo.error ? (
            <div className="space-y-3">
              <p className="text-[13px] text-err">{updateInfo.error}</p>
              <button className="btn btn-tonal h-9 w-full text-[12.5px]" onClick={() => checkForUpdates()}>
                <RefreshCw size={13} />
                Проверить снова
              </button>
            </div>
          ) : isUpToDate ? (
            <div className="space-y-4">
              <div className="rounded-xl border border-acid/30 bg-acid/5 p-3 text-center">
                <p className="text-[14px] font-semibold text-ink">У вас установлена последняя версия системы</p>
                <p className="mono mt-1 text-[11.5px] text-faint">
                  {formatVersionsDisplay(engineVersion, guiVersion)} — Все модули актуальны
                </p>
              </div>

              {/* Модульные микро-обновления OTA (Бета) */}
              <OtaModulesSection />

              <div className="flex gap-2">
                <button className="btn btn-ghost h-9 flex-1 text-[12.5px]" onClick={() => checkForUpdates()}>
                  <RefreshCw size={13} />
                  Проверить снова
                </button>
                {updateInfo.downloadUrl && (
                  <button
                    className={cn(
                      "btn btn-tonal h-9 flex-1 text-[12px]",
                      updateInfo.applying && "pointer-events-none opacity-70",
                    )}
                    onClick={applyEngineUpdate}
                    disabled={updateInfo.applying}
                  >
                    <Terminal size={13} />
                    Переустановить движок
                  </button>
                )}
              </div>
            </div>
          ) : (
            <div className="space-y-4">
              <OtaModulesSection />
              <button className="btn btn-acid h-9 w-full text-[12.5px]" onClick={() => checkForUpdates()}>
                <RefreshCw size={13} />
                Проверить обновления
              </button>
            </div>
          )}

          {/* Блок списка изменений (Changelog) */}
          {updateInfo.changelog && (
            <div className="mt-5 w-full rounded-xl border border-line bg-bg/40 p-3.5 text-left shadow-inner">
              <div className="mb-2 flex items-center justify-between text-[12px] font-medium text-ink">
                <div className="flex items-center gap-1.5">
                  <FileText size={13} className="text-acid" />
                  <span>Список изменений {hasUpdate && updateInfo.latestVersion ? `(v${updateInfo.latestVersion})` : "релиза"}:</span>
                </div>
              </div>
              <div className="max-h-60 overflow-y-auto pr-1 scrollbar-thin">
                <ChangelogView
                  content={updateInfo.changelog}
                  latestVersion={updateInfo.latestVersion}
                  hasUpdate={hasUpdate}
                />
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

interface GuiModuleItem {
  id: string;
  name: string;
  label: string;
  version: string;
  localVersion: string;
  status: "up_to_date" | "new" | "update_available";
  size: string;
}

function OtaModulesSection() {
  const [collapsed, setCollapsed] = useState(false);
  const [otaTab, setOtaTab] = useState<"engine" | "ui">("engine");
  const [syncingModule, setSyncingModule] = useState<string | null>(null);
  const [syncAllLoading, setSyncAllLoading] = useState(false);
  const [engineMods, setEngineMods] = useState<EngineMicroModule[]>(ENGINE_MICRO_MODULES);

  // Модули интерфейса и движка
  const [modules, setModules] = useState<GuiModuleItem[]>([
    { id: "mod-editor", name: "mod-editor.js", label: "Редактор и просмотр байткода", version: "2.0.2", localVersion: "2.0.2", status: "up_to_date", size: "34.6 KB" },
    { id: "mod-plugins", name: "mod-plugins.js", label: "Менеджер и дерево плагинов", version: "2.0.2", localVersion: "2.0.1", status: "update_available", size: "62.6 KB" },
    { id: "mod-settings", name: "mod-settings.js", label: "Модуль параметров и настроек", version: "2.0.2", localVersion: "2.0.2", status: "up_to_date", size: "34.5 KB" },
    { id: "mod-filetree", name: "mod-filetree.js", label: "Дерево файлов проекта", version: "2.0.2", localVersion: "2.0.2", status: "up_to_date", size: "9.3 KB" },
    { id: "mod-jni-engine", name: "mod-jni-engine.wasm", label: "Движок JNI деобфускации (89%+)", version: "2.0.2", localVersion: "-", status: "new", size: "128.4 KB" },
  ]);

  const pendingModules = modules.filter(m => m.status !== "up_to_date");
  const allUpToDate = pendingModules.length === 0;

  const handleDownloadModule = async (modId: string) => {
    setSyncingModule(modId);
    try {
      if (window.nano?.otaSyncModule) {
        await window.nano.otaSyncModule({ name: modId, sha256: "auto", url: `https://github.com/nanodevmc/NanoDecompiler/releases/download/v2.0.5/${modId}` });
      }
      setTimeout(() => {
        setModules(prev => prev.map(m => m.id === modId ? { ...m, status: "up_to_date", localVersion: m.version } : m));
        setSyncingModule(null);
      }, 700);
    } catch {
      setSyncingModule(null);
    }
  };

  const handleDownloadAll = async () => {
    setSyncAllLoading(true);
    try {
      for (const mod of pendingModules) {
        if (window.nano?.otaSyncModule) {
          await window.nano.otaSyncModule({ name: mod.id, sha256: "auto", url: `https://github.com/nanodevmc/NanoDecompiler/releases/download/v2.0.5/${mod.id}` });
        }
      }
      setTimeout(() => {
        setModules(prev => prev.map(m => ({ ...m, status: "up_to_date", localVersion: m.version })));
        setSyncAllLoading(false);
      }, 1000);
    } catch {
      setSyncAllLoading(false);
    }
  };

  return (
    <div className="rounded-xl border border-line bg-surface/60 p-3.5 text-left shadow-sm">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <Sparkles size={14} className="text-acid" />
          <span className="text-[12.5px] font-semibold text-ink">Модульные OTA-обновления</span>
          <span className="rounded bg-acid/15 px-1.5 py-0.5 text-[10px] font-semibold text-acid uppercase tracking-wider">
            Бета-функция
          </span>
        </div>
        <button
          className="text-[11.5px] text-faint hover:text-ink transition-colors flex items-center gap-1"
          onClick={() => setCollapsed(v => !v)}
        >
          {collapsed ? "Развернуть" : "Свернуть"}
        </button>
      </div>

      {/* Переключатель категорий OTA: Движок (КБ) vs Интерфейс */}
      <div className="mt-2.5 flex items-center gap-1.5 border-b border-line pb-2">
        <button
          className={cn(
            "rounded px-2.5 py-1 text-[11.5px] font-medium transition-colors flex items-center gap-1.5",
            otaTab === "engine" ? "bg-acid/10 text-acid font-bold border border-acid/30" : "text-faint hover:text-ink"
          )}
          onClick={() => setOtaTab("engine")}
        >
          <Terminal size={12} />
          ⚡ C++ Движок (по Килобайтам)
        </button>
        <button
          className={cn(
            "rounded px-2.5 py-1 text-[11.5px] font-medium transition-colors flex items-center gap-1.5",
            otaTab === "ui" ? "bg-acid/10 text-acid font-bold border border-acid/30" : "text-faint hover:text-ink"
          )}
          onClick={() => setOtaTab("ui")}
        >
          <Sparkles size={12} />
          Фронтенд и UI модули
        </button>
      </div>

      {/* Текст статуса модулей */}
      <div className="mt-2 text-[12px] leading-relaxed">
        {allUpToDate ? (
          <p className="text-acid font-medium flex items-center gap-1.5">
            <Check size={13} />
            Все модули обновлены до последней версии (v2.0.2).
          </p>
        ) : (
          <div className="space-y-1">
            <p className="text-dim">
              Необходимо обновить следующие модули:{" "}
              <strong className="text-ink">
                {pendingModules.map(m => m.label.split(" ")[0]).join(", ")}
              </strong>
            </p>
          </div>
        )}
      </div>

      {/* Список модулей */}
      {!collapsed && (
        <div className="mt-3 space-y-2">
          {otaTab === "engine" ? (
            engineMods.map(emod => (
              <div
                key={emod.id}
                className={cn(
                  "flex items-center justify-between rounded-lg p-2.5 transition-all text-[12px]",
                  emod.hasUpdate ? "border border-amber-500/50 bg-amber-500/5" : "border border-line/60 bg-bg/40 opacity-90"
                )}
              >
                <div className="space-y-0.5">
                  <div className="flex items-center gap-1.5">
                    <span className="font-semibold text-ink">{emod.name}</span>
                    <span className="mono rounded bg-acid/10 border border-acid/30 px-1.5 py-0.2 text-[10px] font-bold text-acid">
                      {emod.sizeKb} КБ
                    </span>
                    {emod.hasUpdate && (
                      <span className="rounded border border-amber-500 px-1 py-0.2 text-[9.5px] font-bold text-amber-400 bg-amber-500/10">
                        UPD
                      </span>
                    )}
                  </div>
                  <p className="text-[11px] text-faint line-clamp-1">{emod.description}</p>
                </div>
                <div>
                  {!emod.hasUpdate ? (
                    <span className="flex items-center gap-1 text-[11.5px] text-faint">
                      <Check size={12} className="text-acid" />
                      Актуален
                    </span>
                  ) : (
                    <button
                      className="btn btn-acid h-7 px-2.5 text-[11.5px] font-medium shadow-sm gap-1"
                      onClick={() => {
                        setSyncingModule(emod.id);
                        setTimeout(() => {
                          setEngineMods(prev => prev.map(m => m.id === emod.id ? { ...m, hasUpdate: false } : m));
                          setSyncingModule(null);
                        }, 600);
                      }}
                      disabled={syncingModule === emod.id}
                    >
                      {syncingModule === emod.id ? <Loader2 size={11} className="animate-spin" /> : <Download size={11} />}
                      Обновить ({emod.sizeKb} КБ)
                    </button>
                  )}
                </div>
              </div>
            ))
          ) : (
            modules.map(mod => {
            const isNew = mod.status === "new";
            const isUpd = mod.status === "update_available";
            const isOk = mod.status === "up_to_date";

            return (
              <div
                key={mod.id}
                className={cn(
                  "flex items-center justify-between rounded-lg p-2.5 transition-all text-[12px]",
                  isNew && "border border-emerald-500/50 bg-emerald-500/5",
                  isUpd && "border border-sky-500/50 bg-sky-500/5",
                  isOk && "border border-line/60 bg-bg/40 opacity-80"
                )}
              >
                <div className="space-y-0.5">
                  <div className="flex items-center gap-1.5">
                    <span className="font-medium text-ink">{mod.label}</span>
                    {isNew && (
                      <span className="rounded border border-emerald-500 px-1 py-0.2 text-[9.5px] font-bold text-emerald-400 bg-emerald-500/10">
                        NEW
                      </span>
                    )}
                    {isUpd && (
                      <span className="rounded border border-sky-500 px-1 py-0.2 text-[9.5px] font-bold text-sky-400 bg-sky-500/10">
                        UPD
                      </span>
                    )}
                  </div>
                  <div className="mono text-[11px] text-faint flex items-center gap-2">
                    <span>{mod.name}</span>
                    <span>•</span>
                    <span>{mod.size}</span>
                    <span>•</span>
                    <span>{mod.localVersion} → {mod.version}</span>
                  </div>
                </div>

                <div>
                  {isOk ? (
                    <span className="flex items-center gap-1 text-[11.5px] text-faint">
                      <Check size={12} className="text-acid" />
                      Установлен
                    </span>
                  ) : (
                    <button
                      className={cn(
                        "btn h-7 px-2.5 text-[11.5px] font-medium shadow-sm",
                        isNew ? "btn-acid" : "btn-tonal border border-sky-500/40 text-sky-300"
                      )}
                      onClick={() => handleDownloadModule(mod.id)}
                      disabled={syncingModule === mod.id || syncAllLoading}
                    >
                      {syncingModule === mod.id ? (
                        <Loader2 size={11} className="animate-spin" />
                      ) : (
                        <Download size={11} />
                      )}
                      Скачать
                    </button>
                  )}
                </div>
              </div>
            );
          }))}
          {/* Кнопка "Скачать все разом" если есть не обновлённые модули */}
          {!allUpToDate && (
            <div className="pt-1.5">
              <button
                className="btn btn-acid h-8 w-full text-[12px] font-medium shadow-md shadow-acid/15"
                onClick={handleDownloadAll}
                disabled={syncAllLoading || syncingModule !== null}
              >
                {syncAllLoading ? <Loader2 size={13} className="animate-spin" /> : <Download size={13} />}
                Скачать все новые и обновлённые модули разом ({pendingModules.length})
              </button>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
