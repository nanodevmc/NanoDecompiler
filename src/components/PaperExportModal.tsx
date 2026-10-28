import { useState } from "react";
import { FolderUp, Check, Copy, Download, Code2, AlertTriangle, ShieldCheck, X } from "lucide-react";

export function PaperExportModal({
  open,
  onClose,
  pluginName = "GeneratedPlugin",
  version = "1.0.0",
  mainClass = "org.example.Main",
  decompiledFiles = [],
}: {
  open: boolean;
  onClose: () => void;
  pluginName?: string;
  version?: string;
  mainClass?: string;
  decompiledFiles?: { path: string; content?: string }[];
}) {
  const [buildSystem, setBuildSystem] = useState<"maven" | "gradle">("maven");
  const [minecraftVersion, setMinecraftVersion] = useState("1.21.4");
  const [supportFolia, setSupportFolia] = useState(true);
  const [copied, setCopied] = useState(false);

  if (!open) return null;

  // Анализ кода на несовместимости с Folia
  const foliaWarnings: { file: string; line: number; issue: string }[] = [];
  if (supportFolia && decompiledFiles) {
    decompiledFiles.forEach((f) => {
      if (!f.content) return;
      const lines = f.content.split("\n");
      lines.forEach((line, idx) => {
        if (line.includes("Bukkit.getScheduler()") || line.includes("getServer().getScheduler()")) {
          foliaWarnings.push({
            file: f.path,
            line: idx + 1,
            issue: "Глобальный планировщик BukkitScheduler не поддерживается в Folia. Используйте RegionScheduler / GlobalRegionScheduler.",
          });
        }
        if (line.includes(".teleport(") && line.includes("async")) {
          foliaWarnings.push({
            file: f.path,
            line: idx + 1,
            issue: "Асинхронный телепорт сущности в Folia запрещён. Используйте entity.teleportAsync().",
          });
        }
      });
    });
  }

  const pomXml = `<?xml version="1.0" encoding="UTF-8"?>
<project xmlns="http://maven.apache.org/POM/4.0.0"
         xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/xsd/maven-4.0.0.xsd">
    <modelVersion>4.0.0</modelVersion>

    <groupId>com.nanodev</groupId>
    <artifactId>${pluginName.toLowerCase()}</artifactId>
    <version>${version}</version>
    <packaging>jar</packaging>

    <name>${pluginName}</name>

    <properties>
        <java.version>21</java.version>
        <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
    </properties>

    <repositories>
        <repository>
            <id>papermc-repo</id>
            <url>https://repo.papermc.io/repository/maven-public/</url>
        </repository>
    </repositories>

    <dependencies>
        <dependency>
            <groupId>io.papermc.paper</groupId>
            <artifactId>paper-api</artifactId>
            <version>${minecraftVersion}-R0.1-SNAPSHOT</version>
            <scope>provided</scope>
        </dependency>
    </dependencies>

    <build>
        <plugins>
            <plugin>
                <groupId>org.apache.maven.plugins</groupId>
                <artifactId>maven-compiler-plugin</artifactId>
                <version>3.13.0</version>
                <configuration>
                    <source>\${java.version}</source>
                    <target>\${java.version}</target>
                </configuration>
            </plugin>
        </plugins>
    </build>
</project>`;

  const buildGradleKts = `plugins {
    java
    ` + "`maven-publish`" + `
}

group = "com.nanodev"
version = "${version}"

repositories {
    mavenCentral()
    maven("https://repo.papermc.io/repository/maven-public/")
}

dependencies {
    compileOnly("io.papermc.paper:paper-api:${minecraftVersion}-R0.1-SNAPSHOT")
}

java {
    toolchain.languageVersion.set(JavaLanguageVersion.of(21))
}

tasks.withType<JavaCompile> {
    options.encoding = "UTF-8"
}`;

  const paperPluginYml = `name: ${pluginName}
version: '${version}'
main: ${mainClass}
api-version: '1.21'
${supportFolia ? "folia-supported: true" : ""}
author: NanoDev
description: Decompiled and converted to Paper/Folia by NanoDecompiler`;

  const activeContent = buildSystem === "maven" ? pomXml : buildGradleKts;

  function handleCopy() {
    navigator.clipboard.writeText(activeContent);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  }

  function handleDownload() {
    const filename = buildSystem === "maven" ? "pom.xml" : "build.gradle.kts";
    const blob = new Blob([activeContent], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
  }

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm p-4">
      <div className="bg-panel border border-edge rounded-2xl w-full max-w-4xl max-h-[85vh] flex flex-col shadow-2xl overflow-hidden animate-in fade-in zoom-in-95 duration-150">
        {/* Header */}
        <div className="px-6 py-4 border-b border-edge flex items-center justify-between bg-surface/50">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-xl bg-accent/10 text-accent">
              <FolderUp size={22} />
            </div>
            <div>
              <h2 className="text-lg font-bold text-ink">Экспорт проекта в Paper / Folia 1.21.x</h2>
              <p className="text-xs text-ink/60">Автоматическая генерация Maven/Gradle структуры и проверка Folia</p>
            </div>
          </div>
          <button onClick={onClose} className="p-1.5 rounded-lg text-ink/50 hover:text-ink hover:bg-surface transition-colors">
            <X size={18} />
          </button>
        </div>

        {/* Settings Bar */}
        <div className="px-6 py-3 border-b border-edge bg-surface/30 flex items-center justify-between gap-4 flex-wrap">
          <div className="flex items-center gap-3">
            <div className="flex rounded-lg border border-edge p-0.5 bg-panel">
              <button
                onClick={() => setBuildSystem("maven")}
                className={`px-3 py-1 rounded-md text-xs font-medium transition-colors ${buildSystem === "maven" ? "bg-accent text-white" : "text-ink/70 hover:text-ink"}`}
              >
                Maven (pom.xml)
              </button>
              <button
                onClick={() => setBuildSystem("gradle")}
                className={`px-3 py-1 rounded-md text-xs font-medium transition-colors ${buildSystem === "gradle" ? "bg-accent text-white" : "text-ink/70 hover:text-ink"}`}
              >
                Gradle (Kotlin DSL)
              </button>
            </div>

            <div className="flex items-center gap-2 text-xs text-ink/80">
              <span>Версия Paper:</span>
              <select
                value={minecraftVersion}
                onChange={(e) => setMinecraftVersion(e.target.value)}
                className="bg-panel border border-edge rounded px-2 py-1 text-xs text-ink focus:outline-none"
              >
                <option value="1.21.4">1.21.4 (Latest)</option>
                <option value="1.21.1">1.21.1</option>
                <option value="1.20.6">1.20.6</option>
                <option value="1.20.4">1.20.4</option>
              </select>
            </div>

            <label className="flex items-center gap-2 text-xs text-ink/80 cursor-pointer">
              <input
                type="checkbox"
                checked={supportFolia}
                onChange={(e) => setSupportFolia(e.target.checked)}
                className="rounded accent-accent"
              />
              <span>Поддержка Folia (multithread)</span>
            </label>
          </div>

          <div className="flex items-center gap-2">
            <button
              onClick={handleCopy}
              className="flex items-center gap-1.5 px-3 py-1 rounded-lg text-xs font-medium bg-panel hover:bg-surface text-ink transition-colors border border-edge"
            >
              {copied ? <Check size={13} className="text-emerald-400" /> : <Copy size={13} />}
              {copied ? "Скопировано!" : "Копировать"}
            </button>
            <button
              onClick={handleDownload}
              className="flex items-center gap-1.5 px-3 py-1 rounded-lg text-xs font-medium bg-accent hover:bg-accent/90 text-white transition-colors"
            >
              <Download size={13} />
              Скачать файл
            </button>
          </div>
        </div>

        {/* Folia Warnings */}
        {supportFolia && foliaWarnings.length > 0 && (
          <div className="px-6 py-2.5 bg-amber-500/10 border-b border-amber-500/20 flex items-start gap-3">
            <AlertTriangle size={18} className="text-amber-400 mt-0.5 shrink-0" />
            <div className="text-xs text-amber-200/90 space-y-1">
              <span className="font-semibold text-amber-400">Предупреждения совместимости с Folia ({foliaWarnings.length}):</span>
              <ul className="list-disc list-inside space-y-0.5 text-[11px] max-h-20 overflow-y-auto">
                {foliaWarnings.slice(0, 5).map((w, i) => (
                  <li key={i}>
                    <span className="font-mono text-amber-300">{w.file}:{w.line}</span> — {w.issue}
                  </li>
                ))}
              </ul>
            </div>
          </div>
        )}

        {/* Code Preview */}
        <div className="p-6 overflow-y-auto flex-1 font-mono text-xs bg-black/40 text-ink/90">
          <pre className="whitespace-pre-wrap select-all">
            <code>{activeContent}</code>
          </pre>
        </div>

        {/* Paper-plugin.yml footer note */}
        <div className="px-6 py-3 border-t border-edge bg-surface/50 flex items-center justify-between text-xs text-ink/60">
          <span>Также генерируется <code className="text-accent font-mono">paper-plugin.yml</code> для Paper 1.20+</span>
          <button
            onClick={() => {
              navigator.clipboard.writeText(paperPluginYml);
              alert("Скопирован paper-plugin.yml в буфер обмена!");
            }}
            className="text-accent hover:underline flex items-center gap-1"
          >
            <Code2 size={13} /> Скопировать paper-plugin.yml
          </button>
        </div>
      </div>
    </div>
  );
}
