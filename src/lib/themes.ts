// themes.ts — глобальная система тем оформления для NanoDecompiler

export interface ThemeColors {
  bg: string;
  surface: string;
  raised: string;
  overlay: string;
  line: string;
  lineStrong: string;
  ink: string;
  dim: string;
  faint: string;
  acid: string;
  warn: string;
  err: string;
  // Синтаксис Java
  tokK: string; // ключевые слова
  tokS: string; // строки
  tokC: string; // комментарии
  tokA: string; // аннотации
  tokN: string; // числа
}

export interface Theme {
  id: string;
  name: string;
  description?: string;
  author?: string;
  isCustom?: boolean;
  colors: ThemeColors;
}

export const BUILTIN_THEMES: Theme[] = [
  {
    id: "terminal-noir",
    name: "Terminal Noir",
    description: "Классический глубокий терминал с неоновым изумрудом",
    author: "NanoDev",
    colors: {
      bg: "#070a08",
      surface: "#0c100d",
      raised: "#131814",
      overlay: "#1b221d",
      line: "#1f2721",
      lineStrong: "#334139",
      ink: "#eaf0ea",
      dim: "#9aa69e",
      faint: "#5f6b62",
      acid: "#9bf246",
      warn: "#ffb224",
      err: "#ff6f61",
      tokK: "#9bf246",
      tokS: "#e8c487",
      tokC: "#7d8b80",
      tokA: "#ffb224",
      tokN: "#e8985e",
    },
  },
  {
    id: "cyber-obsidian",
    name: "Cyber Obsidian",
    description: "Ультра-тёмный графит с мятным неоном и чистым контрастом",
    author: "NanoDev",
    colors: {
      bg: "#090b0e",
      surface: "#0f1318",
      raised: "#151b22",
      overlay: "#1c242e",
      line: "#212936",
      lineStrong: "#333f52",
      ink: "#f1f5f9",
      dim: "#94a3b8",
      faint: "#64748b",
      acid: "#10b981",
      warn: "#f59e0b",
      err: "#ef4444",
      tokK: "#38bdf8",
      tokS: "#fde047",
      tokC: "#64748b",
      tokA: "#c084fc",
      tokN: "#fb923c",
    },
  },
  {
    id: "tokyo-night",
    name: "Tokyo Night",
    description: "Глубокий индиго ночного Токио с неоновыми акцентами",
    author: "folke",
    colors: {
      bg: "#16161e",
      surface: "#1a1b26",
      raised: "#1f2335",
      overlay: "#24283b",
      line: "#292e42",
      lineStrong: "#3b4261",
      ink: "#c0caf5",
      dim: "#9aa5ce",
      faint: "#565f89",
      acid: "#7aa2f7",
      warn: "#e0af68",
      err: "#f7768e",
      tokK: "#bb9af7",
      tokS: "#9ece6a",
      tokC: "#565f89",
      tokA: "#e0af68",
      tokN: "#ff9e64",
    },
  },
  {
    id: "dracula",
    name: "Dracula",
    description: "Контрастная готическая палитра с фиолетовым и циан акцентами",
    author: "Zeno Rocha",
    colors: {
      bg: "#1e1f29",
      surface: "#282a36",
      raised: "#343746",
      overlay: "#44475a",
      line: "#44475a",
      lineStrong: "#6272a4",
      ink: "#f8f8f2",
      dim: "#b0b4c5",
      faint: "#6272a4",
      acid: "#50fa7b",
      warn: "#ffb86c",
      err: "#ff5555",
      tokK: "#ff79c6",
      tokS: "#f1fa8c",
      tokC: "#6272a4",
      tokA: "#bd93f9",
      tokN: "#bd93f9",
    },
  },
  {
    id: "catppuccin-mocha",
    name: "Catppuccin Mocha",
    description: "Мягкая пастельная палитра для комфортного долгого чтения кода",
    author: "Catppuccin Org",
    colors: {
      bg: "#181825",
      surface: "#1e1e2e",
      raised: "#26283b",
      overlay: "#313244",
      line: "#313244",
      lineStrong: "#45475a",
      ink: "#cdd6f4",
      dim: "#a6adc8",
      faint: "#6c7086",
      acid: "#a6e3a1",
      warn: "#f9e2af",
      err: "#f38ba8",
      tokK: "#cba6f7",
      tokS: "#f9e2af",
      tokC: "#6c7086",
      tokA: "#89b4fa",
      tokN: "#fab387",
    },
  },
  {
    id: "monokai-pro",
    name: "Monokai Pro",
    description: "Классический теплый графит с золотыми и зелеными искрами",
    author: "Wimer Hazenberg",
    colors: {
      bg: "#221f22",
      surface: "#2d2a2e",
      raised: "#363337",
      overlay: "#403e41",
      line: "#403e41",
      lineStrong: "#5b595c",
      ink: "#fcfcfa",
      dim: "#c1c0c0",
      faint: "#727072",
      acid: "#ffd866",
      warn: "#fc9867",
      err: "#ff6188",
      tokK: "#ff6188",
      tokS: "#a9dc76",
      tokC: "#727072",
      tokA: "#78dce8",
      tokN: "#ab9df2",
    },
  },
  {
    id: "nord",
    name: "Nord Frost",
    description: "Ледяной северный арктический минимализм",
    author: "Arctic Ice Studio",
    colors: {
      bg: "#242933",
      surface: "#2e3440",
      raised: "#3b4252",
      overlay: "#434c5e",
      line: "#434c5e",
      lineStrong: "#4c566a",
      ink: "#eceff4",
      dim: "#d8dee9",
      faint: "#616e88",
      acid: "#88c0d0",
      warn: "#ebcb8b",
      err: "#bf616a",
      tokK: "#81a1c1",
      tokS: "#a3be8c",
      tokC: "#616e88",
      tokA: "#b48ead",
      tokN: "#d08770",
    },
  },
  {
    id: "one-dark",
    name: "One Dark Pro",
    description: "Культовая сбалансированная тема Atom / VS Code",
    author: "Binaryify",
    colors: {
      bg: "#1e2227",
      surface: "#21252b",
      raised: "#282c34",
      overlay: "#2c313a",
      line: "#2c313a",
      lineStrong: "#3e4451",
      ink: "#abb2bf",
      dim: "#9da5b4",
      faint: "#5c6370",
      acid: "#98c379",
      warn: "#e5c07b",
      err: "#e06c75",
      tokK: "#c678dd",
      tokS: "#98c379",
      tokC: "#5c6370",
      tokA: "#61afef",
      tokN: "#d19a66",
    },
  },
];

export function applyTheme(theme: Theme) {
  if (typeof document === "undefined") return;
  const root = document.documentElement;
  const c = theme.colors;

  // Базовые цвета приложения
  root.style.setProperty("--color-bg", c.bg);
  root.style.setProperty("--color-surface", c.surface);
  root.style.setProperty("--color-raised", c.raised);
  root.style.setProperty("--color-overlay", c.overlay);
  root.style.setProperty("--color-line", c.line);
  root.style.setProperty("--color-line-strong", c.lineStrong);

  root.style.setProperty("--color-ink", c.ink);
  root.style.setProperty("--color-dim", c.dim);
  root.style.setProperty("--color-faint", c.faint);

  root.style.setProperty("--color-acid", c.acid);
  root.style.setProperty("--color-warn", c.warn);
  root.style.setProperty("--color-err", c.err);

  // Цвета токенов Java в редакторе и диффе
  root.style.setProperty("--tok-k", c.tokK || c.acid);
  root.style.setProperty("--tok-s", c.tokS || "#e8c487");
  root.style.setProperty("--tok-c", c.tokC || "#7d8b80");
  root.style.setProperty("--tok-a", c.tokA || c.warn);
  root.style.setProperty("--tok-n", c.tokN || "#e8985e");

  root.setAttribute("data-theme", theme.id);
}

export function getEffectiveTheme(id: string, customThemes: Theme[] = []): Theme {
  const all = [...BUILTIN_THEMES, ...customThemes];
  return all.find(t => t.id === id) || BUILTIN_THEMES[0];
}
