import { useState, useEffect } from "react";
import { ShieldAlert, ShieldCheck, AlertTriangle, FileCode, CheckCircle2, X, RefreshCw, Bug } from "lucide-react";

export interface SecurityIssue {
  severity: "CRITICAL" | "HIGH" | "MEDIUM" | "INFO";
  category: string;
  description: string;
  file: string;
  line?: number;
  snippet?: string;
}

export function SecurityAuditModal({
  open,
  onClose,
  files,
  onOpenFile,
}: {
  open: boolean;
  onClose: () => void;
  files: { path: string; content?: string }[];
  onOpenFile?: (path: string, line?: number) => void;
}) {
  const [scanning, setScanning] = useState(false);
  const [issues, setIssues] = useState<SecurityIssue[]>([]);
  const [filterSeverity, setFilterSeverity] = useState<string>("ALL");

  useEffect(() => {
    if (open) {
      runAudit();
    }
  }, [open, files]);

  function runAudit() {
    setScanning(true);
    const found: SecurityIssue[] = [];

    // Эвристический сканер безопасности байткода и декомпилированного Java-кода
    for (const f of files) {
      const code = f.content || "";
      if (!code) continue;

      const lines = code.split("\n");

      lines.forEach((line, idx) => {
        const lineNum = idx + 1;
        const trimmed = line.trim();

        // 1. ForceOP / Прямая выдача оператора
        if (trimmed.includes(".setOp(true)") || trimmed.includes("setOp(true)")) {
          found.push({
            severity: "CRITICAL",
            category: "ForceOP / Backdoor",
            description: "Прямая выдача прав оператора игроку (setOp(true))",
            file: f.path,
            line: lineNum,
            snippet: trimmed,
          });
        }

        // 2. Выполнение системных команд / RCE
        if (
          trimmed.includes("Runtime.getRuntime().exec") ||
          trimmed.includes("ProcessBuilder") ||
          (trimmed.includes(".start()") && trimmed.includes("Process"))
        ) {
          found.push({
            severity: "HIGH",
            category: "RCE / Command Execution",
            description: "Вызов системного процесса операционной системы (ProcessBuilder/exec)",
            file: f.path,
            line: lineNum,
            snippet: trimmed,
          });
        }

        // 3. Скрытые вебхуки / Data Exfiltration
        if (
          trimmed.includes("discord.com/api/webhooks") ||
          trimmed.includes("discordapp.com/api/webhooks") ||
          trimmed.includes("api.telegram.org/bot")
        ) {
          found.push({
            severity: "HIGH",
            category: "Data Exfiltration",
            description: "Обнаружена передача данных через внешний вебхук (Discord / Telegram)",
            file: f.path,
            line: lineNum,
            snippet: trimmed,
          });
        }

        // 4. Загрузка внешних классов / Dynamic Payload
        if (trimmed.includes("URLClassLoader") && (trimmed.includes("http://") || trimmed.includes("https://"))) {
          found.push({
            severity: "CRITICAL",
            category: "Dynamic Class Loading",
            description: "Динамическая подгрузка внешнего JAR/класса по сети (URLClassLoader)",
            file: f.path,
            line: lineNum,
            snippet: trimmed,
          });
        }

        // 5. Кража конфигов сервера / Stealer
        if (
          trimmed.includes("server.properties") ||
          trimmed.includes("spigot.yml") ||
          trimmed.includes("paper.yml") ||
          trimmed.includes("rcon.password")
        ) {
          if (trimmed.includes("FileInputStream") || trimmed.includes("Files.read") || trimmed.includes("Scanner")) {
            found.push({
              severity: "MEDIUM",
              category: "Config Stealer / Snooping",
              description: "Чтение системных файлов конфигурации сервера и паролей (server.properties)",
              file: f.path,
              line: lineNum,
              snippet: trimmed,
            });
          }
        }

        // 6. Скрытые сокеты
        if (trimmed.includes("new Socket(") || trimmed.includes("new ServerSocket(")) {
          found.push({
            severity: "MEDIUM",
            category: "Raw Network Sockets",
            description: "Открытие прямого TCP-сокета в обход сетевого стека Minecraft",
            file: f.path,
            line: lineNum,
            snippet: trimmed,
          });
        }
      });
    }

    setIssues(found);
    setScanning(false);
  }

  if (!open) return null;

  const filteredIssues = issues.filter((iss) => (filterSeverity === "ALL" ? true : iss.severity === filterSeverity));
  const criticalCount = issues.filter((i) => i.severity === "CRITICAL").length;
  const highCount = issues.filter((i) => i.severity === "HIGH").length;
  const mediumCount = issues.filter((i) => i.severity === "MEDIUM").length;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm p-4">
      <div className="bg-panel border border-edge rounded-2xl w-full max-w-4xl max-h-[85vh] flex flex-col shadow-2xl overflow-hidden animate-in fade-in zoom-in-95 duration-150">
        {/* Header */}
        <div className="px-6 py-4 border-b border-edge flex items-center justify-between bg-surface/50">
          <div className="flex items-center gap-3">
            <div className={`p-2 rounded-xl ${criticalCount > 0 ? "bg-red-500/10 text-red-400" : highCount > 0 ? "bg-amber-500/10 text-amber-400" : "bg-emerald-500/10 text-emerald-400"}`}>
              {criticalCount > 0 ? <ShieldAlert size={22} /> : highCount > 0 ? <AlertTriangle size={22} /> : <ShieldCheck size={22} />}
            </div>
            <div>
              <h2 className="text-lg font-bold text-ink">Сканер безопасности и бэкдоров</h2>
              <p className="text-xs text-ink/60">Глубокий статический анализ на скрытый вредоносный код</p>
            </div>
          </div>
          <button onClick={onClose} className="p-1.5 rounded-lg text-ink/50 hover:text-ink hover:bg-surface transition-colors">
            <X size={18} />
          </button>
        </div>

        {/* Stats bar */}
        <div className="px-6 py-3 border-b border-edge bg-surface/30 flex items-center justify-between gap-4 flex-wrap">
          <div className="flex items-center gap-2">
            <button
              onClick={() => setFilterSeverity("ALL")}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-colors ${filterSeverity === "ALL" ? "bg-accent text-white" : "bg-panel text-ink/70 hover:bg-surface"}`}
            >
              Все ({issues.length})
            </button>
            <button
              onClick={() => setFilterSeverity("CRITICAL")}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-colors ${filterSeverity === "CRITICAL" ? "bg-red-500 text-white" : "bg-red-500/10 text-red-400 hover:bg-red-500/20"}`}
            >
              Критические ({criticalCount})
            </button>
            <button
              onClick={() => setFilterSeverity("HIGH")}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-colors ${filterSeverity === "HIGH" ? "bg-amber-500 text-white" : "bg-amber-500/10 text-amber-400 hover:bg-amber-500/20"}`}
            >
              Высокие ({highCount})
            </button>
            <button
              onClick={() => setFilterSeverity("MEDIUM")}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-colors ${filterSeverity === "MEDIUM" ? "bg-blue-500 text-white" : "bg-blue-500/10 text-blue-400 hover:bg-blue-500/20"}`}
            >
              Средние ({mediumCount})
            </button>
          </div>

          <button
            onClick={runAudit}
            disabled={scanning}
            className="flex items-center gap-1.5 px-3 py-1 rounded-lg text-xs font-medium bg-panel hover:bg-surface text-ink transition-colors border border-edge"
          >
            <RefreshCw size={13} className={scanning ? "animate-spin" : ""} />
            Пересканировать
          </button>
        </div>

        {/* Issue list */}
        <div className="p-6 overflow-y-auto flex-1 space-y-3">
          {issues.length === 0 ? (
            <div className="py-12 flex flex-col items-center justify-center text-center">
              <div className="p-3 bg-emerald-500/10 text-emerald-400 rounded-full mb-3">
                <CheckCircle2 size={32} />
              </div>
              <h3 className="text-base font-semibold text-ink">Угроз не обнаружено</h3>
              <p className="text-xs text-ink/60 mt-1 max-w-sm">
                Плагин прошел аудит на известные сигнатуры бэкдоров, скрытых RCE и похитителей паролей.
              </p>
            </div>
          ) : (
            filteredIssues.map((issue, idx) => (
              <div
                key={idx}
                className="p-4 rounded-xl border border-edge bg-surface/40 hover:bg-surface/70 transition-all flex flex-col gap-2"
              >
                <div className="flex items-center justify-between gap-2">
                  <div className="flex items-center gap-2">
                    <span
                      className={`px-2 py-0.5 rounded text-[10px] font-bold tracking-wide uppercase ${
                        issue.severity === "CRITICAL"
                          ? "bg-red-500/20 text-red-400 border border-red-500/30"
                          : issue.severity === "HIGH"
                          ? "bg-amber-500/20 text-amber-400 border border-amber-500/30"
                          : "bg-blue-500/20 text-blue-400 border border-blue-500/30"
                      }`}
                    >
                      {issue.severity}
                    </span>
                    <span className="text-sm font-semibold text-ink">{issue.category}</span>
                  </div>
                  <span className="text-xs text-ink/40 font-mono">Строка {issue.line}</span>
                </div>

                <p className="text-xs text-ink/80">{issue.description}</p>

                {issue.snippet && (
                  <div className="p-2 rounded bg-black/40 font-mono text-[11px] text-red-300/90 overflow-x-auto border border-white/5">
                    <code>{issue.snippet}</code>
                  </div>
                )}

                <div className="flex items-center justify-between pt-1">
                  <span className="text-[11px] text-ink/50 font-mono truncate max-w-[80%]">{issue.file}</span>
                  {onOpenFile && (
                    <button
                      onClick={() => onOpenFile(issue.file, issue.line)}
                      className="flex items-center gap-1 text-xs text-accent hover:underline"
                    >
                      <FileCode size={12} />
                      Открыть в коде
                    </button>
                  )}
                </div>
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  );
}
