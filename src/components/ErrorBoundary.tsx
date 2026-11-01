import { Component, type ErrorInfo, type ReactNode } from "react";
import { AlertTriangle, RotateCcw, Copy, Check } from "lucide-react";

interface Props {
  children: ReactNode;
  fallbackTitle?: string;
}

interface State {
  hasError: boolean;
  error: Error | null;
  errorInfo: ErrorInfo | null;
  copied: boolean;
}

export class ErrorBoundary extends Component<Props, State> {
  constructor(props: Props) {
    super(props);
    this.state = {
      hasError: false,
      error: null,
      errorInfo: null,
      copied: false,
    };
  }

  static getDerivedStateFromError(error: Error): Partial<State> {
    return { hasError: true, error };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    console.error("[NanoDecompiler:ErrorBoundary] Uncaught React Error:", error, errorInfo);
    this.setState({ errorInfo });
  }

  private handleReload = () => {
    window.location.reload();
  };

  private handleCopy = () => {
    const { error, errorInfo } = this.state;
    const text = [
      `NanoDecompiler v2.0.5 React Error:`,
      error ? `${error.name}: ${error.message}` : "Unknown error",
      error?.stack ? `\nStack:\n${error.stack}` : "",
      errorInfo?.componentStack ? `\nComponent Stack:\n${errorInfo.componentStack}` : "",
    ].filter(Boolean).join("\n");

    navigator.clipboard.writeText(text).then(() => {
      this.setState({ copied: true });
      setTimeout(() => this.setState({ copied: false }), 2000);
    }).catch(() => {});
  };

  render() {
    if (this.state.hasError) {
      const { error, errorInfo, copied } = this.state;
      const title = this.props.fallbackTitle || "Произошла непредвиденная ошибка в интерфейсе";

      return (
        <div className="relative flex h-full w-full flex-col overflow-hidden bg-bg text-ink select-none">
          {/* Кастомная мини-шапка для закрытия/перезагрузки */}
          <div className="flex h-9 flex-none items-center justify-between border-b border-line bg-surface/90 px-3 z-50">
            <div className="flex items-center gap-2">
              <span className="grid size-5 place-items-center rounded-md border border-warn/40 bg-warn/10 text-warn text-[11px] font-bold">
                !
              </span>
              <span className="text-[12px] font-medium text-dim">NanoDecompiler · Сбой интерфейса</span>
            </div>
            <button
              onClick={this.handleReload}
              className="btn btn-ghost h-6 px-2 text-[11px] flex items-center gap-1 rounded text-dim hover:text-ink"
            >
              <RotateCcw size={12} />
              <span>Перезагрузить</span>
            </button>
          </div>

          <div className="flex flex-1 items-center justify-center p-6 overflow-auto">
            <div className="liquid-glass md3-card max-w-xl w-full p-6 flex flex-col items-center text-center shadow-[0_20px_60px_rgba(0,0,0,0.8)] border border-warn/30">
              <div className="relative flex h-14 w-14 items-center justify-center rounded-2xl border border-warn/40 bg-warn/10 shadow-[0_0_25px_rgba(255,111,97,0.25)] mb-4">
                <AlertTriangle size={28} className="text-warn" />
              </div>

              <h2 className="text-[15px] font-bold text-ink mb-2 tracking-tight">
                {title}
              </h2>

              <p className="text-[12px] text-dim mb-4 max-w-md leading-relaxed">
                Интерфейс перехвачен системой безопасности NanoDecompiler. Данные и скомпилированные файлы на диске сохранены.
              </p>

              {error && (
                <div className="w-full text-left bg-black/40 border border-line rounded-lg p-3 mb-4 max-h-48 overflow-y-auto">
                  <p className="mono text-[11px] font-semibold text-warn mb-1">
                    {error.name}: {error.message}
                  </p>
                  {error.stack && (
                    <pre className="mono text-[9.5px] text-faint whitespace-pre-wrap leading-relaxed">
                      {error.stack}
                    </pre>
                  )}
                  {errorInfo?.componentStack && (
                    <pre className="mono text-[9px] text-faint/70 whitespace-pre-wrap mt-2 pt-2 border-t border-line/40">
                      {errorInfo.componentStack}
                    </pre>
                  )}
                </div>
              )}

              <div className="flex items-center gap-2.5">
                <button
                  onClick={this.handleReload}
                  className="btn btn-acid h-8 px-4 text-xs flex items-center gap-1.5 rounded-lg shadow-[0_0_15px_rgba(155,242,70,0.25)]"
                >
                  <RotateCcw size={13} />
                  <span className="font-semibold">Перезапустить интерфейс</span>
                </button>

                <button
                  onClick={this.handleCopy}
                  className="btn btn-ghost h-8 px-3 text-xs flex items-center gap-1.5 rounded-lg border border-line text-dim hover:text-ink"
                  title="Скопировать отчёт об ошибке для разработчиков"
                >
                  {copied ? <Check size={13} className="text-acid" /> : <Copy size={13} />}
                  <span>{copied ? "Скопировано" : "Скопировать стек"}</span>
                </button>
              </div>
            </div>
          </div>
        </div>
      );
    }

    return this.props.children;
  }
}
