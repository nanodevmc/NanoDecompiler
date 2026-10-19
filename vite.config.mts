import { execSync } from "child_process";
import path from "path";
import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// НОВОЕ v1.8.4 (телеметрия - "коммит с гитхаба берётся"): короткий hash
// текущего коммита, зашивается в сборку через define (см. ниже) - honest
// fallback, если сборка идёт НЕ из git-чекаута (например, распакованный
// исходник без .git) - тогда просто "unknown", а не падение всей сборки.
function getBuildCommit(): string {
  try {
    return execSync("git rev-parse --short HEAD", { cwd: __dirname, stdio: ["ignore", "pipe", "ignore"] })
      .toString()
      .trim();
  } catch {
    return "unknown (not built from git checkout)";
  }
}

export default defineConfig({
  plugins: [react(), tailwindcss()],
  base: "./",
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "src"),
    },
  },
  define: {
    // Глобальная константа-строка (JSON.stringify - иначе Vite вставит как
    // голый идентификатор, а не строковый литерал) - тип объявлен в
    // src/global.d.ts (declare const BUILD_COMMIT: string).
    BUILD_COMMIT: JSON.stringify(getBuildCommit()),
  },
  build: {
    outDir: "dist",
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (id.includes("node_modules")) {
            if (id.includes("lucide-react")) return "mod-icons";
            if (id.includes("react")) return "mod-react-vendor";
            return "mod-vendor";
          }
          if (id.includes("/src/components/SettingsModal") || id.includes("/src/components/SetupWizard")) {
            return "mod-settings";
          }
          if (id.includes("/src/components/PluginDetailsModal") || id.includes("/src/components/ProjectSearchModal")) {
            return "mod-plugins";
          }
          if (id.includes("/src/components/CodeView") || id.includes("/src/components/FindBar")) {
            return "mod-editor";
          }
          if (id.includes("/src/components/FileTree")) {
            return "mod-filetree";
          }
        },
      },
    },
  },
});
