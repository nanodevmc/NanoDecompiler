import { useState, useMemo } from "react";
import { GitFork, Filter, Search, ArrowRight, ShieldAlert, X, ChevronRight } from "lucide-react";

export interface BukkitEventHandler {
  className: string;
  methodName: string;
  eventName: string;
  priority: "LOWEST" | "LOW" | "NORMAL" | "HIGH" | "HIGHEST" | "MONITOR";
  ignoreCancelled: boolean;
  file: string;
  line: number;
}

export function EventFlowModal({
  open,
  onClose,
  files = [],
  onOpenFile,
}: {
  open: boolean;
  onClose: () => void;
  files: { path: string; content?: string }[];
  onOpenFile?: (path: string, line?: number) => void;
}) {
  const [searchQuery, setSearchQuery] = useState("");
  const [selectedPriority, setSelectedPriority] = useState<string>("ALL");

  const handlers = useMemo(() => {
    const list: BukkitEventHandler[] = [];

    files.forEach((f) => {
      const code = f.content || "";
      if (!code.includes("@EventHandler") && !code.includes("Listener")) return;

      const lines = code.split("\n");
      let currentEventHandler: Partial<BukkitEventHandler> | null = null;

      lines.forEach((line, idx) => {
        const trimmed = line.trim();

        if (trimmed.startsWith("@EventHandler")) {
          let priority: BukkitEventHandler["priority"] = "NORMAL";
          let ignoreCancelled = false;

          const priorityMatch = trimmed.match(/priority\s*=\s*EventPriority\.([A-Z]+)/);
          if (priorityMatch && priorityMatch[1]) {
            priority = priorityMatch[1] as any;
          }

          if (trimmed.includes("ignoreCancelled = true") || trimmed.includes("ignoreCancelled=true")) {
            ignoreCancelled = true;
          }

          currentEventHandler = {
            priority,
            ignoreCancelled,
            line: idx + 1,
            file: f.path,
          };
        } else if (currentEventHandler && (trimmed.startsWith("public void") || trimmed.startsWith("void") || trimmed.startsWith("protected void"))) {
          // Parse method name and event parameter
          const methodMatch = trimmed.match(/void\s+([a-zA-Z0-9_]+)\s*\(\s*([a-zA-Z0-9_]+)\s+/);
          if (methodMatch) {
            const methodName = methodMatch[1];
            const eventName = methodMatch[2];

            const className = f.path.split("/").pop()?.replace(".java", "") || "UnknownClass";

            list.push({
              className,
              methodName,
              eventName,
              priority: currentEventHandler.priority || "NORMAL",
              ignoreCancelled: currentEventHandler.ignoreCancelled || false,
              file: f.path,
              line: currentEventHandler.line || idx + 1,
            });
          }
          currentEventHandler = null;
        }
      });
    });

    return list;
  }, [files]);

  if (!open) return null;

  const filtered = handlers.filter((h) => {
    const matchesSearch =
      h.eventName.toLowerCase().includes(searchQuery.toLowerCase()) ||
      h.className.toLowerCase().includes(searchQuery.toLowerCase()) ||
      h.methodName.toLowerCase().includes(searchQuery.toLowerCase());
    const matchesPriority = selectedPriority === "ALL" || h.priority === selectedPriority;
    return matchesSearch && matchesPriority;
  });

  const priorityOrder = ["LOWEST", "LOW", "NORMAL", "HIGH", "HIGHEST", "MONITOR"];

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm p-4">
      <div className="bg-panel border border-edge rounded-2xl w-full max-w-4xl max-h-[85vh] flex flex-col shadow-2xl overflow-hidden animate-in fade-in zoom-in-95 duration-150">
        {/* Header */}
        <div className="px-6 py-4 border-b border-edge flex items-center justify-between bg-surface/50">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-xl bg-purple-500/10 text-purple-400">
              <GitFork size={22} />
            </div>
            <div>
              <h2 className="text-lg font-bold text-ink">Visual Event Flow (Граф слушателей Bukkit)</h2>
              <p className="text-xs text-ink/60">Все слушатели событий @EventHandler с приоритетами вызовов</p>
            </div>
          </div>
          <button onClick={onClose} className="p-1.5 rounded-lg text-ink/50 hover:text-ink hover:bg-surface transition-colors">
            <X size={18} />
          </button>
        </div>

        {/* Toolbar */}
        <div className="px-6 py-3 border-b border-edge bg-surface/30 flex items-center justify-between gap-4 flex-wrap">
          <div className="flex items-center gap-3 flex-1 min-w-[200px]">
            <div className="relative flex-1 max-w-md">
              <Search size={14} className="absolute left-3 top-1/2 -translate-y-1/2 text-ink/40" />
              <input
                type="text"
                placeholder="Поиск по событию, классу или методу..."
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                className="w-full bg-panel border border-edge rounded-lg pl-9 pr-3 py-1.5 text-xs text-ink placeholder:text-ink/40 focus:outline-none focus:border-accent"
              />
            </div>

            <div className="flex items-center gap-1.5 text-xs text-ink/70">
              <Filter size={13} />
              <select
                value={selectedPriority}
                onChange={(e) => setSelectedPriority(e.target.value)}
                className="bg-panel border border-edge rounded-lg px-2 py-1 text-xs text-ink focus:outline-none"
              >
                <option value="ALL">Все приоритеты</option>
                {priorityOrder.map((p) => (
                  <option key={p} value={p}>{p}</option>
                ))}
              </select>
            </div>
          </div>

          <div className="text-xs text-ink/60">
            Найдено слушателей: <span className="font-semibold text-ink">{filtered.length}</span>
          </div>
        </div>

        {/* Content Table */}
        <div className="p-6 overflow-y-auto flex-1 space-y-2">
          {filtered.length === 0 ? (
            <div className="py-12 flex flex-col items-center justify-center text-center text-ink/60">
              <GitFork size={36} className="mb-2 opacity-30" />
              <p className="text-sm">Слушателей событий не найдено</p>
              <p className="text-xs mt-1">Убедитесь, что в декомпилированном плагине есть классы, реализующие Listener.</p>
            </div>
          ) : (
            <div className="border border-edge rounded-xl overflow-hidden bg-surface/30">
              <table className="w-full text-left text-xs">
                <thead className="bg-surface/70 border-b border-edge text-ink/60 uppercase font-semibold text-[10px] tracking-wider">
                  <tr>
                    <th className="py-2.5 px-4">Событие (Event)</th>
                    <th className="py-2.5 px-4">Приоритет</th>
                    <th className="py-2.5 px-4">Класс и метод</th>
                    <th className="py-2.5 px-4">IgnoreCancelled</th>
                    <th className="py-2.5 px-4 text-right">Действие</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-edge">
                  {filtered.map((h, i) => (
                    <tr key={i} className="hover:bg-surface/50 transition-colors">
                      <td className="py-2.5 px-4 font-mono font-semibold text-accent flex items-center gap-1.5">
                        <ArrowRight size={12} className="text-ink/30" />
                        {h.eventName}
                      </td>
                      <td className="py-2.5 px-4">
                        <span
                          className={`px-2 py-0.5 rounded text-[10px] font-bold font-mono ${
                            h.priority === "MONITOR"
                              ? "bg-purple-500/20 text-purple-300"
                              : h.priority === "HIGHEST"
                              ? "bg-red-500/20 text-red-300"
                              : h.priority === "HIGH"
                              ? "bg-amber-500/20 text-amber-300"
                              : h.priority === "LOWEST"
                              ? "bg-blue-500/20 text-blue-300"
                              : "bg-surface text-ink/80"
                          }`}
                        >
                          {h.priority}
                        </span>
                      </td>
                      <td className="py-2.5 px-4 font-mono text-ink/80">
                        <span className="text-ink font-medium">{h.className}</span>.{h.methodName}()
                      </td>
                      <td className="py-2.5 px-4">
                        {h.ignoreCancelled ? (
                          <span className="text-[10px] text-emerald-400 bg-emerald-500/10 px-1.5 py-0.5 rounded">true</span>
                        ) : (
                          <span className="text-[10px] text-ink/40 bg-surface px-1.5 py-0.5 rounded">false</span>
                        )}
                      </td>
                      <td className="py-2.5 px-4 text-right">
                        {onOpenFile && (
                          <button
                            onClick={() => onOpenFile(h.file, h.line)}
                            className="text-xs text-accent hover:underline inline-flex items-center gap-1"
                          >
                            К коду <ChevronRight size={12} />
                          </button>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
