import { useEffect, useState } from "react";
import { EngineProvider, useEngine } from "./state/engine";
import { Titlebar } from "./components/Titlebar";
import { AppHeader } from "./components/AppHeader";
import { Sidebar } from "./components/Sidebar";
import { Workspace } from "./components/Workspace";
import { Terminal } from "./components/Terminal";
import { StatusBar } from "./components/StatusBar";
import { SettingsModal } from "./components/SettingsModal";
import { UpdateModal } from "./components/UpdateModal";
import { CommandPalette } from "./components/CommandPalette";
import { ProjectSearchModal } from "./components/ProjectSearchModal";
import { BugReportModal } from "./components/BugReportModal";
import { SetupWizard } from "./components/SetupWizard";
import { Toasts } from "./components/Toasts";
import { Code2, Archive, Terminal as TerminalIcon, Settings, FolderOpen, Play } from "lucide-react";
import { cn } from "./utils/cn";

function MobileBottomNav({
  activeTab,
  onSelectTab,
  jobsCount,
}: {
  activeTab: "workspace" | "queue" | "terminal";
  onSelectTab: (tab: "workspace" | "queue" | "terminal") => void;
  jobsCount: number;
}) {
  const tabs = [
    { id: "workspace" as const, label: "Декомпилятор", icon: Code2 },
    { id: "queue" as const, label: "Архивы", icon: Archive, badge: jobsCount > 0 ? jobsCount : null },
    { id: "terminal" as const, label: "Терминал & OTA", icon: TerminalIcon },
  ];

  return (
    <nav className="h-16 flex-none border-t border-line bg-surface/90 backdrop-blur-2xl flex items-center justify-around px-2 z-50">
      {tabs.map((tab) => {
        const Icon = tab.icon;
        const isActive = activeTab === tab.id;
        return (
          <button
            key={tab.id}
            onClick={() => {
              if ((window as any).NanoAndroid?.vibrate) {
                (window as any).NanoAndroid.vibrate(25);
              }
              onSelectTab(tab.id);
            }}
            className={cn(
              "relative flex flex-col items-center justify-center flex-1 py-1 transition-all",
              isActive ? "text-acid font-semibold" : "text-dim hover:text-ink"
            )}
          >
            <div
              className={cn(
                "relative flex h-8 px-5 items-center justify-center rounded-full transition-all duration-200",
                isActive ? "bg-acid/15 border border-acid/30 shadow-[0_0_15px_rgba(155,242,70,0.25)]" : "bg-transparent"
              )}
            >
              <Icon size={19} className={isActive ? "text-acid" : "text-dim"} />
              {tab.badge !== null && (
                <span className="absolute -top-1 -right-1 flex h-4 min-w-[16px] px-1 items-center justify-center rounded-full bg-acid text-[10px] font-bold text-black shadow-sm">
                  {tab.badge}
                </span>
              )}
            </div>
            <span className="text-[10.5px] mt-1 tracking-tight">{tab.label}</span>
          </button>
        );
      })}
    </nav>
  );
}

function MobileHeader({
  onOpenSettings,
  onOpenFile,
  onStartQueue,
  canStart,
}: {
  onOpenSettings: () => void;
  onOpenFile: () => void;
  onStartQueue: () => void;
  canStart: boolean;
}) {
  return (
    <header className="flex h-12 flex-none items-center justify-between border-b border-line bg-surface/90 backdrop-blur-xl px-3 z-40">
      <div className="flex items-center gap-2">
        <span className="grid size-6 flex-none place-items-center rounded-md border border-acid/35 bg-acid/10 text-acid text-xs font-bold shadow-[0_0_10px_rgba(155,242,70,0.3)]">
          N
        </span>
        <span className="text-[13px] font-bold tracking-tight text-ink">NanoDecompiler</span>
        <span className="chip h-[18px] px-1.5 text-[9.5px]">v2.0.3</span>
      </div>

      <div className="flex items-center gap-1.5">
        <button
          onClick={onOpenFile}
          className="btn btn-ghost h-8 px-2.5 text-xs flex items-center gap-1.5 rounded-lg border border-line"
          title="Открыть .jar или .class"
        >
          <FolderOpen size={14} className="text-acid" />
          <span className="text-[11px]">Файл</span>
        </button>

        {canStart && (
          <button
            onClick={onStartQueue}
            className="btn btn-acid h-8 px-3 text-xs flex items-center gap-1.5 rounded-lg shadow-[0_0_15px_rgba(155,242,70,0.35)] animate-pulse"
            title="Запустить декомпиляцию"
          >
            <Play size={13} fill="currentColor" />
            <span className="text-[11px] font-bold">Старт</span>
          </button>
        )}

        <button
          onClick={onOpenSettings}
          className="icon-btn h-8 w-8 rounded-lg text-dim hover:text-ink"
          title="Настройки"
        >
          <Settings size={15} />
        </button>
      </div>
    </header>
  );
}

function Shell() {
  const {
    settingsOpen,
    setSettingsOpen,
    updateModalOpen,
    paletteOpen,
    projectSearchOpen,
    setProjectSearchOpen,
    bugReportOpen,
    setBugReportOpen,
    settingsLoaded,
    settings,
    jobs,
    openFileDialog,
    startQueue,
  } = useEngine();

  const [isMobile, setIsMobile] = useState(() => {
    if (typeof window === "undefined") return false;
    return window.innerWidth < 768 || /Android|iPhone|iPad/i.test(navigator.userAgent);
  });

  const [activeMobileTab, setActiveMobileTab] = useState<"workspace" | "queue" | "terminal">("workspace");

  useEffect(() => {
    const handleResize = () => {
      setIsMobile(window.innerWidth < 768 || /Android|iPhone|iPad/i.test(navigator.userAgent));
    };
    window.addEventListener("resize", handleResize);
    return () => window.removeEventListener("resize", handleResize);
  }, []);

  useEffect(() => {
    try {
      const savedBtnStyle = localStorage.getItem("nano:button_style") || "default";
      document.documentElement.setAttribute("data-button-style", savedBtnStyle);
      const savedLayout = localStorage.getItem("nano:layout_preset") || "classic";
      document.documentElement.setAttribute("data-layout-preset", savedLayout);
    } catch {}

    try {
      const savedFont = localStorage.getItem("nano:editor_font");
      if (savedFont === "fira") {
        document.documentElement.style.setProperty("--font-mono", '"Fira Code", ui-monospace, "SF Mono", Menlo, monospace');
      } else if (savedFont === "consolas") {
        document.documentElement.style.setProperty("--font-mono", 'Consolas, Monaco, "Courier New", monospace');
      } else {
        document.documentElement.style.setProperty("--font-mono", '"JetBrains Mono", ui-monospace, "SF Mono", Menlo, monospace');
      }
    } catch {}

    const prevent = (e: DragEvent) => e.preventDefault();
    window.addEventListener("dragover", prevent);
    window.addEventListener("drop", prevent);
    return () => {
      window.removeEventListener("dragover", prevent);
      window.removeEventListener("drop", prevent);
    };
  }, []);

  if (!settingsLoaded) {
    return <div className="h-screen w-screen bg-bg" />;
  }
  if (!settings.setupCompleted) {
    return <SetupWizard />;
  }

  const hasPendingJobs = jobs.some((j) => j.status === "queued" || j.status === "failed");

  return (
    <div className="relative flex h-screen w-screen flex-col overflow-hidden bg-bg text-ink">
      {/* Жидкий неоновый эмбиент на фоне */}
      <div className="absolute inset-0 pointer-events-none overflow-hidden z-0">
        <div className="absolute -top-32 -left-32 w-[380px] h-[380px] rounded-full bg-acid/[0.06] blur-[90px] animate-cyber-glow" />
        <div className="absolute -bottom-32 -right-32 w-[400px] h-[400px] rounded-full bg-sky-400/[0.06] blur-[100px] animate-cyber-glow-alt" />
      </div>

      {isMobile ? (
        /* Мобильный полноэкранный интерфейс Material Design 3 */
        <div className="relative z-10 flex h-full w-full flex-col overflow-hidden">
          <MobileHeader
            onOpenSettings={() => setSettingsOpen(true)}
            onOpenFile={openFileDialog}
            onStartQueue={startQueue}
            canStart={hasPendingJobs}
          />

          <main className="relative flex-1 min-h-0 w-full overflow-hidden flex flex-col">
            {activeMobileTab === "workspace" && (
              <div className="h-full w-full flex flex-col overflow-hidden">
                <Workspace />
              </div>
            )}
            {activeMobileTab === "queue" && (
              <div className="h-full w-full flex flex-col overflow-hidden">
                <Sidebar />
              </div>
            )}
            {activeMobileTab === "terminal" && (
              <div className="h-full w-full flex flex-col overflow-hidden">
                <Terminal />
              </div>
            )}
          </main>

          <MobileBottomNav
            activeTab={activeMobileTab}
            onSelectTab={setActiveMobileTab}
            jobsCount={jobs.length}
          />
        </div>
      ) : (
        /* Классический многооконный десктопный интерфейс */
        <div className="relative z-10 flex h-full w-full min-w-[960px] flex-col overflow-hidden">
          <Titlebar />
          <AppHeader />
          <main className="relative flex min-h-0 flex-1">
            <Sidebar />
            <section className="flex min-w-0 flex-1 flex-col">
              <Workspace />
              <Terminal />
            </section>
          </main>
          <StatusBar />
        </div>
      )}

      {settingsOpen && <SettingsModal />}
      {updateModalOpen && <UpdateModal />}
      {paletteOpen && <CommandPalette />}
      {projectSearchOpen && <ProjectSearchModal onClose={() => setProjectSearchOpen(false)} />}
      {bugReportOpen && <BugReportModal onClose={() => setBugReportOpen(false)} />}
      <Toasts />
    </div>
  );
}

export default function App() {
  return (
    <EngineProvider>
      <Shell />
    </EngineProvider>
  );
}
