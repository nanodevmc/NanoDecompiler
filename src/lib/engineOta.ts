// engineOta.ts — микро-модульная OTA-архитектура для C++ ядра NanoDecompiler
// Разбивка нативного движка на гранулярные килобайтные модули.

export interface EngineMicroModule {
  id: string;
  name: string;
  version: string;
  sizeKb: number;
  category: "core" | "deobf" | "structurer" | "signatures" | "emitter";
  description: string;
  isInstalled: boolean;
  hasUpdate: boolean;
  sha256?: string;
  downloadUrl?: string;
}

export const ENGINE_MICRO_MODULES: EngineMicroModule[] = [
  {
    id: "engine-core",
    name: "NanoCore Native Runtime",
    version: "2.0.2",
    sizeKb: 320,
    category: "core",
    description: "Нативный диспетчер CLI, ZIP-парсер, пул констант и многопоточный воркер",
    isInstalled: true,
    hasUpdate: false,
  },
  {
    id: "mod-aegis-vm",
    name: "AegisVM & AegisIR Deobfuscator",
    version: "2.1.0",
    sizeKb: 42,
    category: "deobf",
    description: "Оффлайн-деобфускация .aeg блобов, AegisIndyBootstrap и инлайн-расшифровка строк",
    isInstalled: true,
    hasUpdate: true,
  },
  {
    id: "mod-cfg-structurer",
    name: "CFG Flow Structurer & Reducer",
    version: "2.0.9",
    sizeKb: 84,
    category: "structurer",
    description: "Свёртка lookupswitch ловушек, бесконечных циклов goto и нередуцируемых ветвей",
    isInstalled: true,
    hasUpdate: true,
  },
  {
    id: "mod-string-decryptor",
    name: "Polymorphic String Decryptor",
    version: "2.0.4",
    sizeKb: 26,
    category: "deobf",
    description: "Многоключевые XOR, UTF-16 преобразования и распаковка строковых массивов",
    isInstalled: true,
    hasUpdate: false,
  },
  {
    id: "mod-java-emitter",
    name: "Clean Java AST Emitter",
    version: "2.0.3",
    sizeKb: 58,
    category: "emitter",
    description: "Форматирование Java 8/17/21 кода, очистка фейковых скобок и расстановка аннотаций",
    isInstalled: true,
    hasUpdate: false,
  },
  {
    id: "mod-pom-synthesizer",
    name: "Maven / Gradle Auto-Builder",
    version: "2.0.1",
    sizeKb: 16,
    category: "emitter",
    description: "Детекция публичных зависимостей (Spigot, Paper, Vault, PRTree) и сборка pom.xml",
    isInstalled: true,
    hasUpdate: false,
  },
  {
    id: "mod-nanoguard-heuristic",
    name: "NanoGuard 25-Stage Filter",
    version: "2.0.5",
    sizeKb: 14,
    category: "signatures",
    description: "25-уровневая защита от вредоносных JAR, краш-бомб и скрытых загрузчиков",
    isInstalled: true,
    hasUpdate: false,
  },
  {
    id: "rules-bytecode-signatures",
    name: "Bytecode Trap Signatures DB",
    version: "2026.10.22",
    sizeKb: 8,
    category: "signatures",
    description: "Килобайтные микро-сигнатуры ловушек Aegis, Allatori, Zelix KlassMaster и Stringer",
    isInstalled: true,
    hasUpdate: true,
  },
];
