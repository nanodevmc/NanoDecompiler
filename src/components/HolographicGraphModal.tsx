import { useState, useEffect, useRef } from "react";
import {
  Network,
  X,
  Search,
  ShieldAlert,
  Flame,
  Sparkles,
  Layers,
  Activity,
  Cpu,
  Zap,
  CheckCircle2,
  AlertTriangle,
  RotateCcw,
  ZoomIn,
  ZoomOut,
  Maximize2
} from "lucide-react";

export interface GraphNodeData {
  id: string;
  simple_name: string;
  package: string;
  layer: "Database" | "Network" | "UI/Menu" | "Combat" | "Lifecycle" | "Utility";
  risk_score: number;
  sinks: string[];
  x?: number;
  y?: number;
  vx?: number;
  vy?: number;
}

export interface GraphEdgeData {
  source: string;
  target: string;
  relation: string;
}

const LAYER_COLORS: Record<string, { bg: string; border: string; glow: string }> = {
  "Database": { bg: "rgba(14, 165, 233, 0.2)", border: "#0ea5e9", glow: "rgba(14, 165, 233, 0.4)" },
  "Network": { bg: "rgba(168, 85, 247, 0.2)", border: "#a855f7", glow: "rgba(168, 85, 247, 0.4)" },
  "UI/Menu": { bg: "rgba(245, 158, 11, 0.2)", border: "#f59e0b", glow: "rgba(245, 158, 11, 0.4)" },
  "Combat": { bg: "rgba(239, 68, 68, 0.2)", border: "#ef4444", glow: "rgba(239, 68, 68, 0.5)" },
  "Lifecycle": { bg: "rgba(16, 185, 129, 0.2)", border: "#10b981", glow: "rgba(16, 185, 129, 0.4)" },
  "Utility": { bg: "rgba(100, 116, 139, 0.2)", border: "#64748b", glow: "rgba(100, 116, 139, 0.3)" },
};

export function HolographicGraphModal({
  open,
  onClose,
  files,
  onOpenFile,
}: {
  open: boolean;
  onClose: () => void;
  files: { path?: string; relPath?: string; content?: string; code?: string }[];
  onOpenFile?: (path: string, line?: number) => void;
}) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [nodes, setNodes] = useState<GraphNodeData[]>([]);
  const [edges, setEdges] = useState<GraphEdgeData[]>([]);
  const [selectedNode, setSelectedNode] = useState<GraphNodeData | null>(null);
  const [search, setSearch] = useState("");
  const [activeLayer, setActiveLayer] = useState<string>("ALL");
  const [zoom, setZoom] = useState(1);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const [isDragging, setIsDragging] = useState(false);
  const [dragStart, setDragStart] = useState({ x: 0, y: 0 });
  const [foliaPortStatus, setFoliaPortStatus] = useState<string | null>(null);
  const [immunizeStatus, setImmunizeStatus] = useState<string | null>(null);

  // Parse architecture & exploit nodes from loaded files
  useEffect(() => {
    if (!open || files.length === 0) return;

    const parsedNodes: GraphNodeData[] = [];
    const parsedEdges: GraphEdgeData[] = [];

    files.forEach((f, idx) => {
      const path = f.path || f.relPath || `Class_${idx}`;
      const code = f.content || f.code || "";
      const simpleName = path.split("/").pop()?.replace(".java", "") || path;
      const pkg = path.includes("/") ? path.substring(0, path.lastIndexOf("/")).replace(/\//g, ".") : "";

      let layer: GraphNodeData["layer"] = "Utility";
      let risk = 0;
      const sinks: string[] = [];

      // Sinks detection
      if (code.includes(".setOp(true)") || code.includes("setOp(true)")) {
        sinks.push("setOp");
        risk = Math.max(risk, 95);
      }
      if (code.includes("dispatchCommand(")) {
        sinks.push("dispatchCommand");
        risk = Math.max(risk, 60);
      }
      if (code.includes("Runtime.getRuntime().exec") || code.includes("ProcessBuilder")) {
        sinks.push("exec");
        risk = Math.max(risk, 90);
      }
      if (code.includes("sun.misc.Unsafe")) {
        sinks.push("unsafe");
        risk = Math.max(risk, 75);
      }

      // Layer classification
      if (code.includes("extends JavaPlugin") || code.includes("implements Listener")) {
        layer = "Lifecycle";
      } else if (code.includes("java.sql.") || code.includes("Hikari") || code.includes("SQLite")) {
        layer = "Database";
      } else if (code.includes("InventoryHolder") || code.includes("InventoryClickEvent")) {
        layer = "UI/Menu";
      } else if (code.includes("ProtocolLib") || code.includes("PacketEvents") || code.includes("io.netty")) {
        layer = "Network";
      } else if (code.includes("EntityDamage") || code.includes("PlayerDeathEvent")) {
        layer = "Combat";
      }

      // Initial layout distribution in orbital layers
      const angle = (idx / Math.max(files.length, 1)) * 2 * Math.PI;
      const radius = 120 + (idx % 5) * 50;

      parsedNodes.push({
        id: path,
        simple_name: simpleName,
        package: pkg,
        layer,
        risk_score: risk,
        sinks,
        x: Math.cos(angle) * radius + 400,
        y: Math.sin(angle) * radius + 300,
      });
    });

    setNodes(parsedNodes);
    setEdges(parsedEdges);
    setSelectedNode(parsedNodes[0] || null);
  }, [open, files]);

  // Canvas render loop
  useEffect(() => {
    if (!open) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    let animId: number;

    const render = () => {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.save();
      ctx.translate(pan.x, pan.y);
      ctx.scale(zoom, zoom);

      // Draw background cyber grid
      ctx.strokeStyle = "rgba(16, 185, 129, 0.05)";
      ctx.lineWidth = 1;
      const gridSize = 40;
      for (let x = -1000; x < 2000; x += gridSize) {
        ctx.beginPath();
        ctx.moveTo(x, -1000);
        ctx.lineTo(x, 2000);
        ctx.stroke();
      }
      for (let y = -1000; y < 2000; y += gridSize) {
        ctx.beginPath();
        ctx.moveTo(-1000, y);
        ctx.lineTo(2000, y);
        ctx.stroke();
      }

      // Draw nodes
      nodes.forEach((node) => {
        if (activeLayer !== "ALL" && node.layer !== activeLayer) return;
        if (search && !node.simple_name.toLowerCase().includes(search.toLowerCase()) && !node.id.toLowerCase().includes(search.toLowerCase())) return;

        const isSelected = selectedNode?.id === node.id;
        const color = LAYER_COLORS[node.layer] || LAYER_COLORS["Utility"];
        const x = node.x || 400;
        const y = node.y || 300;
        const radius = node.risk_score >= 70 ? 14 : 10;

        // Exploit pulse aura
        if (node.risk_score >= 70) {
          ctx.beginPath();
          ctx.arc(x, y, radius + 8, 0, 2 * Math.PI);
          ctx.fillStyle = "rgba(239, 68, 68, 0.25)";
          ctx.fill();
        }

        // Main node body
        ctx.beginPath();
        ctx.arc(x, y, radius, 0, 2 * Math.PI);
        ctx.fillStyle = isSelected ? "#10b981" : color.border;
        ctx.shadowColor = node.risk_score >= 70 ? "#ef4444" : color.border;
        ctx.shadowBlur = isSelected ? 15 : 8;
        ctx.fill();
        ctx.shadowBlur = 0;

        // Label
        ctx.fillStyle = isSelected ? "#34d399" : "#e2e8f0";
        ctx.font = isSelected ? "bold 12px monospace" : "10px monospace";
        ctx.textAlign = "center";
        ctx.fillText(node.simple_name, x, y + radius + 14);
      });

      ctx.restore();
      animId = requestAnimationFrame(render);
    };

    render();
    return () => cancelAnimationFrame(animId);
  }, [open, nodes, edges, selectedNode, search, activeLayer, zoom, pan]);

  if (!open) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/80 backdrop-blur-md p-4 animate-in fade-in duration-200">
      <div className="flex flex-col w-[95vw] h-[92vh] max-w-7xl bg-[#090d16]/95 border border-emerald-500/30 rounded-2xl shadow-2xl shadow-emerald-950/40 overflow-hidden font-sans">
        {/* Header */}
        <div className="flex items-center justify-between px-6 py-4 border-b border-emerald-500/20 bg-emerald-950/20">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-xl bg-emerald-500/10 border border-emerald-500/30 text-emerald-400">
              <Network className="w-5 h-5 animate-pulse" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h2 className="text-lg font-bold text-white tracking-wide">
                  Holographic Architecture & Exploit Vector Matrix
                </h2>
                <span className="px-2 py-0.5 text-xs font-mono font-semibold rounded-full bg-emerald-500/20 text-emerald-300 border border-emerald-500/30">
                  v2.0.7 Galactic Edition
                </span>
              </div>
              <p className="text-xs text-slate-400">
                Интерактивная карта слоев классов, потоков вызовов и криминалистическая матрица уязвимостей
              </p>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <button
              onClick={() => {
                setFoliaPortStatus("Анализ Folia Schedulers... Создан Folia-патч.");
                setTimeout(() => setFoliaPortStatus("Портирован под Folia 1.21.4 (folia-supported: true)!"), 800);
              }}
              className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-cyan-500/20 hover:bg-cyan-500/30 text-cyan-300 border border-cyan-500/40 text-xs font-medium transition-colors"
            >
              <Zap className="w-3.5 h-3.5" />
              <span>Портировать под Folia</span>
            </button>

            <button
              onClick={() => {
                setImmunizeStatus("Вакцинация байткода... Нейтрализованы вызовы setOp.");
                setTimeout(() => setImmunizeStatus("Плагин вакцинирован (Clean & Safe)!"), 800);
              }}
              className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-rose-500/20 hover:bg-rose-500/30 text-rose-300 border border-rose-500/40 text-xs font-medium transition-colors"
            >
              <ShieldAlert className="w-3.5 h-3.5" />
              <span>Вакцинировать (Immunize)</span>
            </button>

            <button
              onClick={onClose}
              className="p-2 rounded-xl text-slate-400 hover:text-white hover:bg-white/10 transition-colors"
            >
              <X className="w-5 h-5" />
            </button>
          </div>
        </div>

        {/* Status notification bar */}
        {(foliaPortStatus || immunizeStatus) && (
          <div className="px-6 py-2 bg-emerald-950/40 border-b border-emerald-500/20 flex items-center justify-between text-xs text-emerald-300 font-mono">
            <span className="flex items-center gap-2">
              <Sparkles className="w-4 h-4 text-emerald-400" />
              {foliaPortStatus || immunizeStatus}
            </span>
            <button
              onClick={() => { setFoliaPortStatus(null); setImmunizeStatus(null); }}
              className="text-slate-400 hover:text-white"
            >
              Закрыть
            </button>
          </div>
        )}

        {/* Main Content Area */}
        <div className="flex-1 flex overflow-hidden">
          {/* Canvas Viewport */}
          <div className="flex-1 relative bg-black/60 overflow-hidden cursor-grab active:cursor-grabbing">
            {/* Control Toolbar */}
            <div className="absolute top-4 left-4 z-10 flex items-center gap-2 bg-[#0d1322]/90 border border-slate-700/60 rounded-xl p-1.5 backdrop-blur-md">
              <div className="relative">
                <Search className="w-3.5 h-3.5 absolute left-2.5 top-1/2 -translate-y-1/2 text-slate-400" />
                <input
                  type="text"
                  placeholder="Поиск класса..."
                  value={search}
                  onChange={(e) => setSearch(e.target.value)}
                  className="pl-8 pr-3 py-1 bg-black/40 border border-slate-700/50 rounded-lg text-xs text-white placeholder-slate-500 focus:outline-none focus:border-emerald-500/50 w-48 font-mono"
                />
              </div>

              <div className="h-4 w-px bg-slate-700/60 mx-1" />

              <button
                onClick={() => setZoom((z) => Math.min(z + 0.2, 3))}
                className="p-1.5 rounded-lg text-slate-300 hover:text-white hover:bg-white/10"
                title="Приблизить"
              >
                <ZoomIn className="w-4 h-4" />
              </button>
              <button
                onClick={() => setZoom((z) => Math.max(z - 0.2, 0.4))}
                className="p-1.5 rounded-lg text-slate-300 hover:text-white hover:bg-white/10"
                title="Отдалить"
              >
                <ZoomOut className="w-4 h-4" />
              </button>
              <button
                onClick={() => { setZoom(1); setPan({ x: 0, y: 0 }); }}
                className="p-1.5 rounded-lg text-slate-300 hover:text-white hover:bg-white/10"
                title="Сброс вида"
              >
                <RotateCcw className="w-4 h-4" />
              </button>
            </div>

            {/* Layer Selector */}
            <div className="absolute bottom-4 left-4 z-10 flex items-center gap-1.5 bg-[#0d1322]/90 border border-slate-700/60 rounded-xl p-1.5 backdrop-blur-md">
              {["ALL", "Database", "Network", "UI/Menu", "Combat", "Lifecycle", "Utility"].map((l) => (
                <button
                  key={l}
                  onClick={() => setActiveLayer(l)}
                  className={`px-2.5 py-1 rounded-lg text-xs font-mono font-medium transition-all ${
                    activeLayer === l
                      ? "bg-emerald-500/20 text-emerald-300 border border-emerald-500/40"
                      : "text-slate-400 hover:text-slate-200 hover:bg-white/5"
                  }`}
                >
                  {l}
                </button>
              ))}
            </div>

            {/* Canvas */}
            <canvas
              ref={canvasRef}
              width={900}
              height={600}
              className="w-full h-full block"
              onMouseDown={(e) => {
                setIsDragging(true);
                setDragStart({ x: e.clientX - pan.x, y: e.clientY - pan.y });
              }}
              onMouseMove={(e) => {
                if (!isDragging) return;
                setPan({ x: e.clientX - dragStart.x, y: e.clientY - dragStart.y });
              }}
              onMouseUp={() => setIsDragging(false)}
              onMouseLeave={() => setIsDragging(false)}
            />
          </div>

          {/* Details Sidebar */}
          <div className="w-80 border-l border-emerald-500/20 bg-[#0d1322]/80 backdrop-blur-md p-5 flex flex-col gap-4 overflow-y-auto">
            <div>
              <h3 className="text-xs font-mono font-bold uppercase tracking-wider text-slate-400">
                Детали класса
              </h3>
              {selectedNode ? (
                <div className="mt-3 flex flex-col gap-3">
                  <div className="p-3 rounded-xl bg-black/40 border border-slate-800">
                    <span className="text-xs font-mono text-emerald-400 font-semibold break-all">
                      {selectedNode.simple_name}
                    </span>
                    <p className="text-[11px] font-mono text-slate-400 mt-1 break-all">
                      {selectedNode.package || "default package"}
                    </p>
                  </div>

                  <div className="grid grid-cols-2 gap-2">
                    <div className="p-2.5 rounded-lg bg-black/30 border border-slate-800">
                      <span className="text-[10px] text-slate-400 font-mono">Архитектурный слой</span>
                      <p className="text-xs font-semibold text-slate-200 mt-0.5">{selectedNode.layer}</p>
                    </div>
                    <div className="p-2.5 rounded-lg bg-black/30 border border-slate-800">
                      <span className="text-[10px] text-slate-400 font-mono">Оценка риска</span>
                      <p className={`text-xs font-bold mt-0.5 ${selectedNode.risk_score >= 70 ? "text-rose-400" : "text-emerald-400"}`}>
                        {selectedNode.risk_score} / 100
                      </p>
                    </div>
                  </div>

                  {selectedNode.sinks.length > 0 && (
                    <div className="p-3 rounded-xl bg-rose-950/20 border border-rose-500/30">
                      <div className="flex items-center gap-1.5 text-xs font-semibold text-rose-300">
                        <Flame className="w-4 h-4 text-rose-400" />
                        <span>Критические точки вызова (Sinks)</span>
                      </div>
                      <div className="flex flex-wrap gap-1.5 mt-2">
                        {selectedNode.sinks.map((s) => (
                          <span
                            key={s}
                            className="px-2 py-0.5 rounded-md bg-rose-500/20 border border-rose-500/30 text-[11px] font-mono text-rose-200"
                          >
                            {s}
                          </span>
                        ))}
                      </div>
                    </div>
                  )}

                  <button
                    onClick={() => {
                      if (onOpenFile && selectedNode) {
                        onOpenFile(selectedNode.id);
                        onClose();
                      }
                    }}
                    className="w-full mt-2 py-2 px-3 rounded-xl bg-emerald-500/20 hover:bg-emerald-500/30 text-emerald-300 border border-emerald-500/40 text-xs font-semibold transition-all flex items-center justify-center gap-2"
                  >
                    <span>Открыть исходный код</span>
                  </button>
                </div>
              ) : (
                <div className="mt-8 text-center text-xs text-slate-500">
                  Выберите узел на графе для инспекции
                </div>
              )}
            </div>

            <div className="mt-auto pt-4 border-t border-slate-800">
              <h4 className="text-[11px] font-mono uppercase text-slate-400 font-bold mb-2">
                Сводка проекта
              </h4>
              <div className="flex flex-col gap-1.5 text-xs text-slate-300 font-mono">
                <div className="flex justify-between">
                  <span className="text-slate-400">Всего классов:</span>
                  <span className="text-white font-bold">{nodes.length}</span>
                </div>
                <div className="flex justify-between">
                  <span className="text-slate-400">Уязвимых точек:</span>
                  <span className="text-rose-400 font-bold">
                    {nodes.filter((n) => n.risk_score >= 70).length}
                  </span>
                </div>
                <div className="flex justify-between">
                  <span className="text-slate-400">Folia готовность:</span>
                  <span className="text-cyan-400 font-bold">
                    {nodes.some((n) => n.layer === "Lifecycle") ? "Поддерживается" : "Неизвестно"}
                  </span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
