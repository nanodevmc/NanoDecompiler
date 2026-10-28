import { useState, useEffect } from "react";
import {
  ListTree,
  Terminal,
  Shield,
  Headphones,
  FileCode,
  X,
  Search,
  Check,
  Copy,
  Binary,
  KeyRound,
  Eye,
  ChevronRight,
  ShieldAlert,
  Flame,
  FileText,
  FolderSync,
  Network,
  Lock,
  Unlock,
  Sparkles,
} from "lucide-react";
import { useEngine } from "../state/engine";

export interface BytecodeInstruction {
  pc: number;
  opcode: number;
  mnemonic: string;
  operand?: string | number;
  comment?: string;
}

export interface BytecodeMethod {
  name: string;
  descriptor: string;
  accessFlags: number;
  isStatic: boolean;
  isPublic: boolean;
  annotations: string[];
  instructions: BytecodeInstruction[];
  insnSummary?: string;
}

export interface BytecodeField {
  name: string;
  descriptor: string;
  accessFlags: number;
  isStatic: boolean;
}

export interface BytecodeClass {
  name: string;
  superName: string;
  interfaces: string[];
  fields: BytecodeField[];
  methods: BytecodeMethod[];
  annotations: string[];
  isListener: boolean;
  isCommandExecutor: boolean;
  rawStringConstants: string[];
}

export interface BytecodeEvent {
  eventClass: string;
  methodName: string;
  listenerClass: string;
  priority?: string;
  ignoreCancelled?: boolean;
}

export interface BytecodeCommand {
  command: string;
  sourceClass: string;
  method: string;
}

export interface BytecodePermission {
  permission: string;
  sourceClass: string;
  method: string;
}

export interface DecryptedStringInfo {
  original: string;
  decrypted: string;
  sourceClass: string;
  method?: string;
  algorithm: string;
}

export interface SecurityVulnerability {
  severity: "CRITICAL" | "HIGH" | "MEDIUM" | "LOW";
  category: "SQL_INJECTION" | "COMMAND_INJECTION" | "RCE" | "ARBITRARY_FILE";
  title: string;
  description: string;
  className: string;
  methodName: string;
  pc: number;
}

export interface DrmCheckFinding {
  hostOrEndpoint?: string;
  methodName: string;
  className: string;
  confidence: "HIGH" | "MEDIUM";
  details: string;
  suggestedPatch: {
    description: string;
    patchHex: string;
  };
}

export interface MemoryLeakRisk {
  type: "STATIC_PLAYER_COLLECTION" | "UNREGISTERED_TASK" | "DANGLING_LISTENER";
  severity: "HIGH" | "MEDIUM";
  className: string;
  fieldName?: string;
  methodName?: string;
  explanation: string;
}

export interface XRefItem {
  target: string;
  callers: Array<{ className: string; methodName: string }>;
  callees: Array<{ className: string; methodName: string }>;
}

export interface SemanticRenameSuggestion {
  originalName: string;
  suggestedName: string;
  reason: string;
  confidence: number;
}

export interface PluginTreeData {
  jar: string;
  manifest: {
    name?: string;
    version?: string;
    main?: string;
    api_version?: string;
    author?: string;
  };
  commands: Array<{
    command: string;
    description?: string;
    permission?: string;
    aliases?: string;
  }>;
  permissions: Array<{
    permission: string;
    description?: string;
    default?: string;
  }>;
  listeners: string[];
  events?: BytecodeEvent[];
  bytecodeCommands?: BytecodeCommand[];
  bytecodePermissions?: BytecodePermission[];
  decryptedStrings?: DecryptedStringInfo[];
  vulnerabilities?: SecurityVulnerability[];
  drmChecks?: DrmCheckFinding[];
  memoryLeaks?: MemoryLeakRisk[];
  xrefs?: Record<string, XRefItem>;
  renameSuggestions?: SemanticRenameSuggestion[];
  opaqueTrapsCount?: number;
  classes?: BytecodeClass[];
  stats?: {
    totalClasses: number;
    totalMethods: number;
    totalInstructions: number;
    totalStrings: number;
    decryptedStringsCount: number;
    vulnerabilitiesCount?: number;
    drmChecksCount?: number;
    memoryLeaksCount?: number;
  };
}

export function PluginTreeModal({
  open,
  onClose,
  treeData,
}: {
  open: boolean;
  onClose: () => void;
  treeData: PluginTreeData | null;
}) {
  const { settings, saveSettings, copyText, toast } = useEngine();
  const [activeTab, setActiveTab] = useState<
    "commands" | "permissions" | "events" | "bytecode" | "decrypted" | "security" | "wiki" | "manifest"
  >("commands");
  const [search, setSearch] = useState("");
  const [copied, setCopied] = useState(false);
  const [selectedClassIdx, setSelectedClassIdx] = useState(0);
  const [selectedMethodIdx, setSelectedMethodIdx] = useState<number | null>(null);
  const [wikiMarkdown, setWikiMarkdown] = useState<string>("");
  const [wikiLoading, setWikiLoading] = useState(false);

  const isDetailed = settings.detailedPluginInfo ?? false;

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape") onClose();
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [onClose]);

  if (!open || !treeData) return null;

  const m = treeData.manifest;
  const classes = treeData.classes || [];
  const events = treeData.events || [];
  const decryptedStrings = treeData.decryptedStrings || [];
  const vulnerabilities = treeData.vulnerabilities || [];
  const drmChecks = treeData.drmChecks || [];
  const memoryLeaks = treeData.memoryLeaks || [];
  const xrefs = treeData.xrefs || {};
  const renameSuggestions = treeData.renameSuggestions || [];
  const stats = treeData.stats;

  const filteredCommands = treeData.commands.filter(
    (c) =>
      c.command.toLowerCase().includes(search.toLowerCase()) ||
      (c.description && c.description.toLowerCase().includes(search.toLowerCase())) ||
      (c.permission && c.permission.toLowerCase().includes(search.toLowerCase()))
  );

  const filteredPermissions = treeData.permissions.filter(
    (p) =>
      p.permission.toLowerCase().includes(search.toLowerCase()) ||
      (p.description && p.description.toLowerCase().includes(search.toLowerCase()))
  );

  const filteredEvents = events.filter(
    (e) =>
      e.eventClass.toLowerCase().includes(search.toLowerCase()) ||
      e.listenerClass.toLowerCase().includes(search.toLowerCase()) ||
      e.methodName.toLowerCase().includes(search.toLowerCase())
  );

  const filteredDecrypted = decryptedStrings.filter(
    (d) =>
      d.original.toLowerCase().includes(search.toLowerCase()) ||
      d.decrypted.toLowerCase().includes(search.toLowerCase()) ||
      d.sourceClass.toLowerCase().includes(search.toLowerCase()) ||
      d.algorithm.toLowerCase().includes(search.toLowerCase())
  );

  const filteredClasses = classes.filter(
    (c) =>
      c.name.toLowerCase().includes(search.toLowerCase()) ||
      c.methods.some((m) => m.name.toLowerCase().includes(search.toLowerCase()))
  );

  const selectedClass = classes[selectedClassIdx] || classes[0];
  const selectedMethod =
    selectedMethodIdx !== null && selectedClass ? selectedClass.methods[selectedMethodIdx] : null;

  // X-Refs для выбранного метода
  const selectedMethodKey =
    selectedClass && selectedMethod ? `${selectedClass.name}.${selectedMethod.name}()` : "";
  const selectedXref = selectedMethodKey ? xrefs[selectedMethodKey] : null;

  function copyJson() {
    navigator.clipboard.writeText(JSON.stringify(treeData, null, 2));
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  }

  function toggleDetailed() {
    saveSettings({ ...settings, detailedPluginInfo: !isDetailed });
  }

  async function handleLoadWiki() {
    setActiveTab("wiki");
    if (!wikiMarkdown) {
      setWikiLoading(true);
      try {
        const res = await (window.nano as any).generatePluginWiki(treeData);
        if (res.ok && res.markdown) {
          setWikiMarkdown(res.markdown);
        }
      } catch (e) {
        console.error(e);
      } finally {
        setWikiLoading(false);
      }
    }
  }

  async function handleExportIde(type: "vscode" | "idea") {
    try {
      const res = await (window.nano as any).exportToIde({
        outDir: ".",
        ideType: type,
        pluginName: m.name || treeData?.jar || "Plugin",
      });
      if (res.ok) {
        toast(`Настройки для ${type.toUpperCase()} успешно созданы!`, "ok");
      } else {
        toast(res.error || "Ошибка экспорта в IDE", "err");
      }
    } catch (e: any) {
      toast(e?.message || "Ошибка", "err");
    }
  }

  return (
    <div className="fixed inset-0 z-[100] flex items-center justify-center bg-black/60 backdrop-blur-sm p-4">
      <div className="relative flex flex-col w-full max-w-4xl h-[86vh] overflow-hidden rounded-xl border border-line bg-surface shadow-2xl animate-rise">
        {/* Заголовок */}
        <div className="flex items-center justify-between border-b border-line px-5 py-3 bg-raised/40 flex-none">
          <div className="flex items-center gap-2.5">
            <ListTree size={18} className="text-acid" />
            <div>
              <h3 className="font-semibold text-ink text-sm flex items-center gap-2">
                Дерево архитектуры: {m.name || treeData.jar}{" "}
                <span className="text-faint text-[11px] font-mono">v{m.version || "1.0"}</span>
                {stats && (
                  <span className="px-1.5 py-0.5 rounded bg-raised border border-line text-[10px] font-mono text-acid">
                    {stats.totalClasses} кл. · {stats.totalMethods} мет.
                  </span>
                )}
                {vulnerabilities.length > 0 && (
                  <span className="px-1.5 py-0.5 rounded bg-red-500/10 border border-red-500/30 text-[10px] font-mono text-red-400 flex items-center gap-1">
                    <ShieldAlert size={10} /> {vulnerabilities.length} уязвимостей
                  </span>
                )}
              </h3>
              <div className="text-[10px] text-faint">
                Main: <span className="font-mono text-ink/80">{m.main || "Unknown"}</span> · API:{" "}
                <span className="font-mono text-ink/80">{m.api_version || "legacy"}</span>
              </div>
            </div>
          </div>
          <div className="flex items-center gap-2">
            {/* Кнопка Wiki */}
            <button
              onClick={handleLoadWiki}
              className={`btn text-xs inline-flex items-center gap-1.5 py-1 px-2.5 border transition-all ${
                activeTab === "wiki"
                  ? "bg-acid/15 border-acid text-acid"
                  : "bg-raised border-line text-faint hover:text-ink"
              }`}
              title="Сгенерировать Markdown Wiki / README по плагину"
            >
              <FileText size={13} />
              <span>Wiki / README</span>
            </button>

            {/* Экспорт в IDE */}
            <div className="flex items-center rounded-lg border border-line bg-raised overflow-hidden text-xs">
              <button
                onClick={() => handleExportIde("vscode")}
                className="px-2 py-1 text-faint hover:text-ink hover:bg-raised/80 transition-colors"
                title="Экспорт настроек в VS Code (.vscode)"
              >
                VS Code
              </button>
              <span className="w-px h-3 bg-line" />
              <button
                onClick={() => handleExportIde("idea")}
                className="px-2 py-1 text-faint hover:text-ink hover:bg-raised/80 transition-colors"
                title="Экспорт настроек в IntelliJ IDEA (.idea)"
              >
                IDEA
              </button>
            </div>

            {/* Тумблер Детального режима */}
            <button
              onClick={toggleDetailed}
              className={`btn text-xs inline-flex items-center gap-1.5 py-1 px-2.5 border transition-all ${
                isDetailed
                  ? "bg-acid/15 border-acid text-acid shadow-sm shadow-acid/20"
                  : "bg-raised border-line text-faint hover:text-ink"
              }`}
              title="Переключить детальный режим (показ сырого байткода и опкодов)"
            >
              <Eye size={13} className={isDetailed ? "text-acid" : "text-faint"} />
              <span>Детально: {isDetailed ? "ВКЛ" : "ВЫКЛ"}</span>
            </button>

            <button
              onClick={copyJson}
              className="btn btn-ghost text-xs inline-flex items-center gap-1.5 py-1 px-2.5"
              title="Копировать JSON"
            >
              {copied ? <Check size={13} className="text-acid" /> : <Copy size={13} />}
              {copied ? "Скопировано!" : "JSON"}
            </button>
            <button
              onClick={onClose}
              className="text-faint hover:text-ink transition-colors p-1 rounded-lg"
              aria-label="Закрыть"
            >
              <X size={16} />
            </button>
          </div>
        </div>

        {/* Навигация по вкладкам и поиск */}
        <div className="flex items-center justify-between border-b border-line px-5 py-2 bg-raised/20 flex-none gap-4">
          <div className="flex gap-1 overflow-x-auto py-0.5">
            <button
              className={`px-2.5 py-1 rounded-lg text-xs font-medium flex items-center gap-1.5 transition-colors whitespace-nowrap ${
                activeTab === "commands"
                  ? "bg-acid/15 text-acid border border-acid/30"
                  : "text-faint hover:text-ink"
              }`}
              onClick={() => setActiveTab("commands")}
            >
              <Terminal size={13} /> Команды ({treeData.commands.length})
            </button>
            <button
              className={`px-2.5 py-1 rounded-lg text-xs font-medium flex items-center gap-1.5 transition-colors whitespace-nowrap ${
                activeTab === "permissions"
                  ? "bg-acid/15 text-acid border border-acid/30"
                  : "text-faint hover:text-ink"
              }`}
              onClick={() => setActiveTab("permissions")}
            >
              <Shield size={13} /> Permissions ({treeData.permissions.length})
            </button>
            <button
              className={`px-2.5 py-1 rounded-lg text-xs font-medium flex items-center gap-1.5 transition-colors whitespace-nowrap ${
                activeTab === "events"
                  ? "bg-acid/15 text-acid border border-acid/30"
                  : "text-faint hover:text-ink"
              }`}
              onClick={() => setActiveTab("events")}
            >
              <Headphones size={13} /> События ({events.length || treeData.listeners.length})
            </button>
            <button
              className={`px-2.5 py-1 rounded-lg text-xs font-medium flex items-center gap-1.5 transition-colors whitespace-nowrap ${
                activeTab === "bytecode"
                  ? "bg-acid/15 text-acid border border-acid/30"
                  : "text-faint hover:text-ink"
              }`}
              onClick={() => setActiveTab("bytecode")}
            >
              <Binary size={13} /> Байткод ({classes.length})
            </button>
            <button
              className={`px-2.5 py-1 rounded-lg text-xs font-medium flex items-center gap-1.5 transition-colors whitespace-nowrap ${
                activeTab === "decrypted"
                  ? "bg-acid/15 text-acid border border-acid/30"
                  : "text-faint hover:text-ink"
              }`}
              onClick={() => setActiveTab("decrypted")}
            >
              <KeyRound size={13} /> Дешифровка ({decryptedStrings.length})
            </button>
            <button
              className={`px-2.5 py-1 rounded-lg text-xs font-medium flex items-center gap-1.5 transition-colors whitespace-nowrap ${
                activeTab === "security"
                  ? "bg-acid/15 text-acid border border-acid/30"
                  : "text-faint hover:text-ink"
              }`}
              onClick={() => setActiveTab("security")}
            >
              <ShieldAlert size={13} /> Аудит & DRM (
              {vulnerabilities.length + drmChecks.length + memoryLeaks.length})
            </button>
            <button
              className={`px-2.5 py-1 rounded-lg text-xs font-medium flex items-center gap-1.5 transition-colors whitespace-nowrap ${
                activeTab === "manifest"
                  ? "bg-acid/15 text-acid border border-acid/30"
                  : "text-faint hover:text-ink"
              }`}
              onClick={() => setActiveTab("manifest")}
            >
              <FileCode size={13} /> Манифест
            </button>
          </div>

          <div className="relative w-44 flex-none">
            <Search size={13} className="absolute left-2.5 top-2.5 text-faint" />
            <input
              type="text"
              placeholder="Фильтр..."
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              className="w-full rounded-lg border border-line bg-raised pl-8 pr-3 py-1 text-ink text-xs outline-none focus:border-acid"
            />
          </div>
        </div>

        {/* Контент вкладок */}
        <div className="flex-1 overflow-y-auto p-5 space-y-2 text-xs">
          {/* 1. КОМАНДЫ */}
          {activeTab === "commands" && (
            <div className="space-y-2">
              {filteredCommands.length === 0 ? (
                <div className="text-center text-faint py-8">Команды не найдены</div>
              ) : (
                filteredCommands.map((c, i) => (
                  <div key={i} className="p-3 rounded-lg border border-line bg-raised/30 space-y-1">
                    <div className="flex items-center justify-between">
                      <span className="font-mono font-semibold text-acid text-sm">{c.command}</span>
                      {c.permission && (
                        <span className="px-2 py-0.5 rounded bg-raised border border-line text-[10px] font-mono text-faint">
                          {c.permission}
                        </span>
                      )}
                    </div>
                    {c.description && <p className="text-ink/80 text-[11px]">{c.description}</p>}
                    {c.aliases && (
                      <div className="text-[10px] text-faint font-mono">
                        Алиасы: {typeof c.aliases === "string" ? c.aliases : JSON.stringify(c.aliases)}
                      </div>
                    )}
                  </div>
                ))
              )}
            </div>
          )}

          {/* 2. ПРАВА */}
          {activeTab === "permissions" && (
            <div className="space-y-2">
              {filteredPermissions.length === 0 ? (
                <div className="text-center text-faint py-8">Права не найдены</div>
              ) : (
                filteredPermissions.map((p, i) => (
                  <div
                    key={i}
                    className="p-3 rounded-lg border border-line bg-raised/30 flex items-start justify-between"
                  >
                    <div>
                      <div className="font-mono font-semibold text-ink text-[11px]">{p.permission}</div>
                      {p.description && <p className="text-faint text-[10px] mt-0.5">{p.description}</p>}
                    </div>
                    <span className="px-2 py-0.5 rounded bg-raised border border-line text-[10px] text-acid font-mono">
                      default: {p.default || "op"}
                    </span>
                  </div>
                ))
              )}
            </div>
          )}

          {/* 3. СОБЫТИЯ */}
          {activeTab === "events" && (
            <div className="space-y-3">
              {filteredEvents.length > 0 && (
                <div>
                  <h4 className="text-[11px] font-semibold text-faint uppercase tracking-wider mb-2">
                    Обработчики событий из байткода (@EventHandler)
                  </h4>
                  <div className="space-y-2">
                    {filteredEvents.map((ev, i) => (
                      <div
                        key={i}
                        className="p-3 rounded-lg border border-line bg-raised/30 flex items-start justify-between gap-3"
                      >
                        <div className="space-y-1">
                          <div className="font-mono font-semibold text-acid text-xs">{ev.eventClass}</div>
                          <div className="text-[10px] text-faint font-mono">
                            Метод: <span className="text-ink">{ev.methodName}()</span> в классе{" "}
                            <span className="text-ink">{ev.listenerClass}</span>
                          </div>
                        </div>
                        <span className="px-2 py-0.5 rounded bg-acid/10 border border-acid/20 text-[10px] font-mono text-acid">
                          @EventHandler
                        </span>
                      </div>
                    ))}
                  </div>
                </div>
              )}

              <div>
                <h4 className="text-[11px] font-semibold text-faint uppercase tracking-wider mb-2">
                  Зарегистрированные слушатели (Listeners)
                </h4>
                <div className="space-y-1.5 font-mono text-[11px]">
                  {treeData.listeners.length === 0 ? (
                    <div className="text-center text-faint py-4">Слушатели эвентов не найдены</div>
                  ) : (
                    treeData.listeners.map((l, i) => (
                      <div
                        key={i}
                        className="p-2.5 rounded-lg border border-line bg-raised/30 flex items-center gap-2"
                      >
                        <Headphones size={13} className="text-acid flex-none" />
                        <span className="text-ink/90 truncate">{l}</span>
                      </div>
                    ))
                  )}
                </div>
              </div>
            </div>
          )}

          {/* 4. БАЙТКОД И X-REFS (Пункт 1) */}
          {activeTab === "bytecode" && (
            <div className="grid grid-cols-12 gap-3 h-full">
              {/* Левая колонка: список классов */}
              <div className="col-span-5 border border-line rounded-lg bg-raised/20 overflow-y-auto max-h-[64vh] p-2 space-y-1">
                <div className="text-[10px] text-faint uppercase tracking-wider px-2 py-1 font-semibold flex items-center justify-between">
                  <span>Классы ({filteredClasses.length})</span>
                  {renameSuggestions.length > 0 && (
                    <span className="text-acid text-[9px] font-mono flex items-center gap-0.5">
                      <Sparkles size={10} /> {renameSuggestions.length} ренеймов
                    </span>
                  )}
                </div>
                {filteredClasses.map((cls, idx) => {
                  const actualIdx = classes.indexOf(cls);
                  const isSel = actualIdx === selectedClassIdx;
                  const rename = renameSuggestions.find((r) => r.originalName === cls.name);
                  return (
                    <button
                      key={cls.name}
                      onClick={() => {
                        setSelectedClassIdx(actualIdx);
                        setSelectedMethodIdx(null);
                      }}
                      className={`w-full text-left px-2.5 py-1.5 rounded-lg text-xs font-mono transition-colors flex items-center justify-between ${
                        isSel
                          ? "bg-acid/15 text-acid border border-acid/30"
                          : "text-ink/80 hover:bg-raised hover:text-ink"
                      }`}
                    >
                      <div className="truncate">
                        <span>{cls.name.split(".").pop()}</span>
                        {rename && (
                          <span className="text-[9px] text-acid/90 block truncate">
                            → {rename.suggestedName}
                          </span>
                        )}
                      </div>
                      <span className="text-[10px] text-faint ml-2 flex-none">
                        {cls.methods.length} m.
                      </span>
                    </button>
                  );
                })}
              </div>

              {/* Правая колонка: методы и перекрестные ссылки X-Refs */}
              <div className="col-span-7 border border-line rounded-lg bg-raised/20 overflow-y-auto max-h-[64vh] p-3 space-y-3">
                {selectedClass ? (
                  <div>
                    <div className="border-b border-line pb-2 mb-3">
                      <div className="font-mono font-semibold text-acid text-xs">{selectedClass.name}</div>
                      <div className="text-[10px] text-faint font-mono">
                        Super: {selectedClass.superName} · Interfaces:{" "}
                        {selectedClass.interfaces.length ? selectedClass.interfaces.join(", ") : "None"}
                      </div>
                    </div>

                    <div className="space-y-1.5">
                      <div className="text-[10px] font-semibold text-faint uppercase tracking-wider">
                        Методы ({selectedClass.methods.length})
                      </div>
                      <div className="space-y-1">
                        {selectedClass.methods.map((meth, mIdx) => {
                          const isMethSel = selectedMethodIdx === mIdx;
                          const mKey = `${selectedClass.name}.${meth.name}()`;
                          const xrefInfo = xrefs[mKey];
                          const callersCount = xrefInfo ? xrefInfo.callers.length : 0;

                          return (
                            <div
                              key={mIdx}
                              className={`p-2 rounded-lg border transition-all ${
                                isMethSel
                                  ? "border-acid/40 bg-acid/10"
                                  : "border-line bg-raised/30 hover:border-line/80"
                              }`}
                            >
                              <div
                                className="flex items-center justify-between cursor-pointer"
                                onClick={() => setSelectedMethodIdx(isMethSel ? null : mIdx)}
                              >
                                <div className="font-mono text-xs text-ink flex items-center gap-1.5 truncate">
                                  <ChevronRight
                                    size={12}
                                    className={`transition-transform flex-none ${
                                      isMethSel ? "rotate-90 text-acid" : "text-faint"
                                    }`}
                                  />
                                  <span className="text-acid font-semibold">{meth.name}</span>
                                  <span className="text-faint text-[10px] truncate">{meth.descriptor}</span>
                                </div>
                                <div className="flex items-center gap-2">
                                  {callersCount > 0 && (
                                    <span
                                      className="px-1.5 py-0.2 rounded bg-raised border border-line text-[9px] font-mono text-faint"
                                      title="Входящие вызовы (X-Refs callers)"
                                    >
                                      {callersCount} call
                                    </span>
                                  )}
                                  <span className="text-[10px] font-mono text-faint flex-none">
                                    {meth.instructions.length} опкодов
                                  </span>
                                </div>
                              </div>

                              {/* Детальный листинг опкодов и X-Refs */}
                              {isMethSel && (
                                <div className="mt-2.5 pt-2 border-t border-line/60 font-mono text-[10px] space-y-2">
                                  {/* Граф входящих вызовов (X-Refs) */}
                                  {xrefInfo && xrefInfo.callers.length > 0 && (
                                    <div className="p-2 rounded bg-black/30 border border-line/50 space-y-1">
                                      <div className="text-[9px] text-acid uppercase font-semibold flex items-center gap-1">
                                        <Network size={10} /> Кто вызывает этот метод (X-Refs callers):
                                      </div>
                                      <div className="max-h-24 overflow-y-auto space-y-0.5">
                                        {xrefInfo.callers.map((c, ci) => (
                                          <div key={ci} className="text-faint truncate">
                                            ← <span className="text-ink">{c.className}</span>.
                                            <span className="text-acid">{c.methodName}()</span>
                                          </div>
                                        ))}
                                      </div>
                                    </div>
                                  )}

                                  {/* Пошаговые опкоды */}
                                  <div className="space-y-1 max-h-56 overflow-y-auto bg-black/40 p-2 rounded">
                                    {meth.instructions.map((insn) => (
                                      <div
                                        key={insn.pc}
                                        className="flex items-center gap-2 hover:bg-white/5 px-1 rounded"
                                      >
                                        <span className="text-faint w-8 flex-none text-right">
                                          {insn.pc}:
                                        </span>
                                        <span className="text-acid font-semibold w-24 flex-none">
                                          {insn.mnemonic}
                                        </span>
                                        {insn.operand !== undefined && (
                                          <span className="text-ink/80 flex-none">{insn.operand}</span>
                                        )}
                                        {insn.comment && (
                                          <span className="text-faint/80 truncate">
                                            // {insn.comment}
                                          </span>
                                        )}
                                      </div>
                                    ))}
                                  </div>
                                </div>
                              )}
                            </div>
                          );
                        })}
                      </div>
                    </div>
                  </div>
                ) : (
                  <div className="text-center text-faint py-12">Выберите класс для просмотра байткода</div>
                )}
              </div>
            </div>
          )}

          {/* 5. ДЕШИФРОВАННЫЕ СТРОКИ */}
          {activeTab === "decrypted" && (
            <div className="space-y-2">
              <div className="p-3 rounded-lg border border-acid/30 bg-acid/10 text-xs text-ink flex items-center justify-between">
                <div>
                  <span className="font-semibold text-acid">
                    Автоматическая деобфускация строк в байткоде:
                  </span>{" "}
                  найдено {decryptedStrings.length} зашифрованных констант.
                </div>
                <span className="px-2 py-0.5 rounded bg-acid/20 text-acid font-mono text-[10px]">
                  Aegis / Allatori / Base64 / XOR
                </span>
              </div>

              {filteredDecrypted.length === 0 ? (
                <div className="text-center text-faint py-12">
                  Зашифрованные строки не обнаружены (строки в открытом виде либо дешифрованы на лету)
                </div>
              ) : (
                <div className="space-y-2">
                  {filteredDecrypted.map((d, i) => (
                    <div
                      key={i}
                      className="p-3 rounded-lg border border-line bg-raised/30 space-y-2 font-mono text-xs"
                    >
                      <div className="flex items-center justify-between">
                        <span className="text-[10px] text-faint font-semibold uppercase tracking-wider">
                          Класс: <span className="text-ink">{d.sourceClass}</span>
                        </span>
                        <span className="px-2 py-0.5 rounded bg-raised border border-line text-[10px] text-acid">
                          {d.algorithm}
                        </span>
                      </div>

                      <div className="grid grid-cols-2 gap-2 text-[11px]">
                        <div className="p-2 rounded bg-black/30 border border-line/60">
                          <div className="text-[9px] text-faint uppercase mb-1">
                            Исходный байткод (Шифр)
                          </div>
                          <div className="text-faint truncate" title={d.original}>
                            {d.original}
                          </div>
                        </div>

                        <div className="p-2 rounded bg-acid/10 border border-acid/30">
                          <div className="text-[9px] text-acid uppercase mb-1 flex items-center justify-between">
                            <span>Расшифрованный текст</span>
                            <button
                              onClick={() => copyText(d.decrypted, "Расшифрованная строка")}
                              className="text-acid hover:underline"
                            >
                              Копия
                            </button>
                          </div>
                          <div className="text-acid font-semibold truncate" title={d.decrypted}>
                            {d.decrypted}
                          </div>
                        </div>
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}

          {/* 6. АУДИТ БЕЗОПАСНОСТИ, УТЕЧКИ И DRM (Пункты 2, 3, 4) */}
          {activeTab === "security" && (
            <div className="space-y-4">
              {/* Уязвимости SQL / Command Injection */}
              <div>
                <h4 className="text-xs font-semibold text-ink flex items-center gap-1.5 uppercase tracking-wider mb-2">
                  <ShieldAlert size={14} className="text-red-400" />
                  Уязвимости кода (SQL & Command Injection)
                </h4>
                {vulnerabilities.length === 0 ? (
                  <div className="p-3 rounded-lg border border-line bg-raised/20 text-faint">
                    Опасных вызовов Statement или сырых команд не обнаружено.
                  </div>
                ) : (
                  <div className="space-y-2">
                    {vulnerabilities.map((v, i) => (
                      <div
                        key={i}
                        className="p-3 rounded-lg border border-red-500/30 bg-red-500/10 space-y-1 text-xs"
                      >
                        <div className="flex items-center justify-between">
                          <span className="font-semibold text-red-400">{v.title}</span>
                          <span className="px-1.5 py-0.5 rounded bg-red-500/20 text-red-300 font-mono text-[10px]">
                            {v.severity} · {v.category}
                          </span>
                        </div>
                        <p className="text-ink/90 text-[11px]">{v.description}</p>
                        <div className="text-[10px] font-mono text-faint">
                          В классе: {v.className}.{v.methodName}() @ pc {v.pc}
                        </div>
                      </div>
                    ))}
                  </div>
                )}
              </div>

              {/* Утечки памяти Bukkit */}
              <div>
                <h4 className="text-xs font-semibold text-ink flex items-center gap-1.5 uppercase tracking-wider mb-2">
                  <Flame size={14} className="text-amber-400" />
                  Потенциальные утечки памяти (Memory Leaks)
                </h4>
                {memoryLeaks.length === 0 ? (
                  <div className="p-3 rounded-lg border border-line bg-raised/20 text-faint">
                    Статических ссылок на Player и висячих слушателей не найдено.
                  </div>
                ) : (
                  <div className="space-y-2">
                    {memoryLeaks.map((l, i) => (
                      <div
                        key={i}
                        className="p-3 rounded-lg border border-amber-500/30 bg-amber-500/10 space-y-1 text-xs"
                      >
                        <div className="font-semibold text-amber-300">{l.type}</div>
                        <p className="text-ink/90 text-[11px]">{l.explanation}</p>
                        <div className="text-[10px] font-mono text-faint">
                          Поле: {l.className}.{l.fieldName}
                        </div>
                      </div>
                    ))}
                  </div>
                )}
              </div>

              {/* Детектор DRM и лицензий с No-DRM патчем */}
              <div>
                <h4 className="text-xs font-semibold text-ink flex items-center gap-1.5 uppercase tracking-wider mb-2">
                  <Lock size={14} className="text-acid" />
                  DRM / Проверки лицензий & Патчер (No-DRM)
                </h4>
                {drmChecks.length === 0 ? (
                  <div className="p-3 rounded-lg border border-line bg-raised/20 text-faint">
                    Внешних сетевых проверок лицензий и DRM-запросов не обнаружено.
                  </div>
                ) : (
                  <div className="space-y-2">
                    {drmChecks.map((d, i) => (
                      <div
                        key={i}
                        className="p-3 rounded-lg border border-acid/30 bg-acid/10 space-y-2 text-xs"
                      >
                        <div className="flex items-center justify-between">
                          <span className="font-semibold text-acid">
                            Сетевая валидация: {d.hostOrEndpoint}
                          </span>
                          <span className="px-1.5 py-0.5 rounded bg-acid/20 text-acid font-mono text-[10px]">
                            Уверенность: {d.confidence}
                          </span>
                        </div>
                        <p className="text-ink/90 text-[11px]">{d.details}</p>
                        <div className="p-2 rounded bg-black/40 border border-line/60 font-mono text-[10px] space-y-1">
                          <div className="text-acid flex items-center justify-between">
                            <span>{d.suggestedPatch.description}</span>
                            <button
                              onClick={() => copyText(d.suggestedPatch.patchHex, "Патч No-DRM")}
                              className="hover:underline flex items-center gap-1"
                            >
                              <Copy size={11} /> Копия байткода
                            </button>
                          </div>
                          <div className="text-faint">{d.suggestedPatch.patchHex}</div>
                        </div>
                      </div>
                    ))}
                  </div>
                )}
              </div>
            </div>
          )}

          {/* 7. WIKI И README (Пункт 15) */}
          {activeTab === "wiki" && (
            <div className="space-y-3">
              <div className="flex items-center justify-between border-b border-line pb-2">
                <div className="text-xs font-semibold text-ink">
                  Автоматическая Wiki и README документация плагина
                </div>
                <button
                  onClick={() => copyText(wikiMarkdown, "Markdown документация")}
                  className="btn btn-ghost text-xs inline-flex items-center gap-1.5 py-1 px-2.5"
                >
                  <Copy size={13} className="text-acid" /> Скопировать Markdown
                </button>
              </div>

              {wikiLoading ? (
                <div className="text-center text-faint py-12">Генерация документации...</div>
              ) : (
                <pre className="p-4 rounded-lg border border-line bg-black/40 font-mono text-[11px] text-ink/90 max-h-[60vh] overflow-y-auto whitespace-pre-wrap select-text">
                  {wikiMarkdown}
                </pre>
              )}
            </div>
          )}

          {/* 8. МАНИФЕСТ */}
          {activeTab === "manifest" && (
            <div className="rounded-lg border border-line bg-raised/30 p-4 font-mono text-[11px] space-y-2 text-ink/90">
              <div>
                <strong className="text-acid">name:</strong> {m.name || "N/A"}
              </div>
              <div>
                <strong className="text-acid">version:</strong> {m.version || "N/A"}
              </div>
              <div>
                <strong className="text-acid">main:</strong> {m.main || "N/A"}
              </div>
              <div>
                <strong className="text-acid">api-version:</strong> {m.api_version || "N/A"}
              </div>
              <div>
                <strong className="text-acid">author:</strong> {m.author || "N/A"}
              </div>
              {stats && (
                <div className="pt-3 mt-3 border-t border-line space-y-1 text-faint">
                  <div>
                    <strong className="text-ink">Всего классов в JAR:</strong> {stats.totalClasses}
                  </div>
                  <div>
                    <strong className="text-ink">Всего методов:</strong> {stats.totalMethods}
                  </div>
                  <div>
                    <strong className="text-ink">Всего инструкций байткода:</strong>{" "}
                    {stats.totalInstructions}
                  </div>
                  <div>
                    <strong className="text-ink">Всего строк в Constant Pool:</strong>{" "}
                    {stats.totalStrings}
                  </div>
                  <div>
                    <strong className="text-ink">Расшифровано строк:</strong>{" "}
                    {stats.decryptedStringsCount}
                  </div>
                </div>
              )}
            </div>
          )}
        </div>

        {/* Футер */}
        <div className="flex items-center justify-between border-t border-line px-5 py-2.5 bg-raised/30 flex-none text-[11px] text-faint">
          <div>
            Детальная информация:{" "}
            <span className={isDetailed ? "text-acid font-semibold" : "text-faint"}>
              {isDetailed ? "Включена" : "Выключена (по умолчанию)"}
            </span>
          </div>
          <div>NanoDecompiler Suite 2.0.2</div>
        </div>
      </div>
    </div>
  );
}
