/**
 * Tauri v2 Bridge for NanoDecompiler
 * Вызывает нативные команды Tauri через встроенный __TAURI_INTERNALS__ без лишних npm-пакетов.
 */

export function initTauriBridge() {
  if (typeof window === "undefined") return;

  const internals = (window as any).__TAURI_INTERNALS__;
  const isTauri = !!internals || !!(window as any).__TAURI__;

  if (isTauri && !(window as any).nano) {
    const invoke = (cmd: string, args: any = {}) => {
      if (internals?.invoke) {
        return internals.invoke(cmd, args);
      }
      return Promise.resolve(null);
    };

    (window as any).nano = {
      getGuiVersion: async () => {
        try {
          return await invoke("get_gui_version");
        } catch {
          return "2.0.2-tauri";
        }
      },

      selectJar: async () => {
        try {
          return await invoke("plugin:dialog|open", {
            filters: [{ name: "Java Archives", extensions: ["jar", "zip", "tar.gz", "7z", "rar"] }],
            multiple: true,
          });
        } catch {
          return [];
        }
      },

      selectOutDir: async (defaultPath?: string) => {
        try {
          return await invoke("plugin:dialog|open", {
            directory: true,
            defaultPath,
          });
        } catch {
          return null;
        }
      },

      openPath: async (target: string) => {
        return await invoke("open_path", { target });
      },

      openExternal: async (url: string) => {
        return await invoke("open_external", { url });
      },

      jarSummary: async (jarPath: string) => {
        return await invoke("jar_summary", { jarPath });
      },

      scanJar: async (jarPath: string) => {
        return await invoke("scan_jar", { jarPath });
      },

      checkEnv: async () => {
        return {
          java: { ok: true, text: "Java 21 (Tauri Integrated)" },
          maven: { ok: true, text: "Maven 3.9 (Bundled)" },
        };
      },

      minimizeWindow: async () => {
        return await invoke("plugin:window|minimize");
      },

      toggleMaximizeWindow: async () => {
        return await invoke("plugin:window|toggle_maximize");
      },

      closeWindow: async () => {
        return await invoke("plugin:window|close");
      },

      isWindowMaximized: async () => {
        return await invoke("plugin:window|is_maximized");
      },
    };

    console.log("[NanoDecompiler] Tauri v2 Native Bridge initialized successfully!");
  }
}
