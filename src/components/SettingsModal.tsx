import { Check, FolderOpen, Loader2, Minus, Plus, Send, X, Palette, FileCode2, ChevronDown, Globe, Layout, Sliders, Sparkles, Paintbrush } from "lucide-react";
import { useEffect, useState, type ReactNode } from "react";
import { useEngine } from "../state/engine";
import { Toggle, Kbd } from "./ui";
import { cn } from "../utils/cn";
import { t, getAvailableLanguages, registerCustomTranslation } from "../lib/i18n";
import { BUILTIN_THEMES, Theme, applyTheme, getEffectiveTheme } from "../lib/themes";
import { DEFAULT_ICON_THUMBNAILS } from "../lib/iconThumbs";
import { formatVersionsDisplay } from "../lib/model";

function Row({ label, hint, control }: { label: string; hint?: ReactNode; control: ReactNode }) {
  return (
    <div className="flex items-center gap-4 py-2.5">
      <div className="min-w-0 flex-1">
        <p className="text-[12.5px] text-ink/90">{label}</p>
        {hint && <p className="mt-0.5 text-[11px] leading-snug text-faint">{hint}</p>}
      </div>
      {control}
    </div>
  );
}

function TelegramCredit({ handle, role }: { handle: string; role: string }) {
  const { toast } = useEngine();
  const url = `https://t.me/${handle}`;
  return (
    <a
      href={url}
      onClick={e => {
        e.preventDefault();
        window.nano.openExternal(url).catch(() => toast("Не удалось открыть ссылку", "err"));
      }}
      className="group flex items-center gap-3 rounded-xl border border-line bg-bg px-3 py-2.5 no-underline transition-colors hover:border-acid/40 hover:bg-acid/5"
    >
      <span className="grid h-8 w-8 flex-none place-items-center rounded-full border border-line bg-surface text-faint group-hover:border-acid/40 group-hover:text-acid">
        <Send size={13} />
      </span>
      <span className="min-w-0 flex-1">
        <span className="block text-[12.5px] text-ink/90">{role}</span>
        <span className="mono block text-[11px] text-faint group-hover:text-acid">t.me/{handle}</span>
      </span>
    </a>
  );
}

type Tab = "general" | "themes" | "layout" | "decompiler" | "editor" | "about";

const EDITOR_FONTS = [
  {
    id: "jetbrains",
    label: "JetBrains Mono",
    fontFamily: '"JetBrains Mono", ui-monospace, "SF Mono", Menlo, monospace',
    desc: "Шрифт для разработчиков с четкими символами и лигатурами",
  },
  {
    id: "fira",
    label: "Fira Code",
    fontFamily: '"Fira Code", ui-monospace, "SF Mono", Menlo, monospace',
    desc: "Популярный моноширинный шрифт Mozilla с лигатурами для кода",
  },
  {
    id: "consolas",
    label: "Consolas",
    fontFamily: 'Consolas, Monaco, "Courier New", monospace',
    desc: "Классический моноширинный шрифт Windows и Visual Studio",
  },
] as const;

export interface LayoutPreset {
  id: string;
  name: string;
  desc: string;
  icon: string;
}

interface ButtonStylePreset {
  id: string;
  name: string;
  desc: string;
}

const BUTTON_STYLES: ButtonStylePreset[] = [
  { id: "default", name: "Классический", desc: "Стандартные закругленные кнопки" },
  { id: "pill", name: "Капсулы / Пилюли", desc: "Максимально круглые, овальные очертания" },
  { id: "neon", name: "Кислотный Неон", desc: "Яркое неоновое свечение и кислотный контур" },
  { id: "bordered", name: "Контурный Pro", desc: "Строгая 2px обводка, технический стиль" },
  { id: "flat", name: "Минимализм Flat", desc: "Плоские кнопки без контуров" },
  { id: "compact", name: "Микро / Компакт", desc: "Плотный уменьшенный размер элементов" },
  { id: "cyberpunk", name: "Cyberpunk Sci-Fi", desc: "Скошенные углы в стиле киберпанк" },
];

const LAYOUT_PRESETS: LayoutPreset[] = [
  { id: "classic", name: "Classic IDE", desc: "Дерево файлов слева, просмотр кода по центру, терминал снизу", icon: "🏛" },
  { id: "zen", name: "Zen Focus", desc: "Максимальная рабочая зона для кода, скрываемое всплывающее дерево", icon: "🧘" },
  { id: "compact_topbar", name: "Compact Topbar", desc: "Минималистичный заголовок с кнопками в одну плотную строку", icon: "⚡" },
  { id: "left_sidebar_pro", name: "Left Sidebar Pro", desc: "Вертикальная колонка инструментов слева (как в VS Code)", icon: "📱" },
  { id: "split_dual", name: "Split Dual", desc: "Двухколоночный просмотр двух файлов одновременно", icon: "🔀" },
  { id: "dock_floating", name: "Dock Floating", desc: "Модульные панели: терминал справа, дерево файлов слева", icon: "🗂" },
];

export function SettingsModal() {
  const {
    settings,
    saveSettings,
    setSettingsOpen,
    envIssue,
    resolveEnvIssue,
    engineVersion,
    guiVersion,
    javaEnv,
    mavenEnv,
    installingTool,
    installProgress,
    installTool,
    addToSystemPath,
    toast,
  } = useEngine();
  const [draft, setDraft] = useState<any>(settings);
  const [checking, setChecking] = useState<"idle" | "busy" | "ok">("idle");
  const [tab, setTab] = useState<Tab>("general");
  const [aboutSubTab, setAboutSubTab] = useState<"overview" | "features" | "team">("overview");
  const [langDropdownOpen, setLangDropdownOpen] = useState(false);
  const [availableLangs, setAvailableLangs] = useState<{ key: string; name: string }[]>(() => getAvailableLanguages());

  // Кастомные темы пользователя (максимум 2)
  const [customThemes, setCustomThemes] = useState<Theme[]>(() => {
    try {
      const saved = localStorage.getItem("nano:custom_themes");
      return saved ? JSON.parse(saved) : [];
    } catch {
      return [];
    }
  });
  const [editingSlot, setEditingSlot] = useState<number>(0);
  const [themeDraftName, setThemeDraftName] = useState("Моя тема 1");
  const [themeDraftBg, setThemeDraftBg] = useState("#0a0d0b");
  const [themeDraftSurface, setThemeDraftSurface] = useState("#121714");
  const [themeDraftAcid, setThemeDraftAcid] = useState("#a3e635");
  const [themeDraftTokK, setThemeDraftTokK] = useState("#38bdf8");
  const [themeDraftTokS, setThemeDraftTokS] = useState("#fde047");

  // Темы стиля кнопок и положений
  const [buttonStyle, setButtonStyle] = useState<string>(() => {
    try {
      return localStorage.getItem("nano:button_style") || "default";
    } catch {
      return "default";
    }
  });

  const selectButtonStyle = (styleId: string) => {
    setButtonStyle(styleId);
    try {
      localStorage.setItem("nano:button_style", styleId);
      document.documentElement.setAttribute("data-button-style", styleId);
    } catch {}
    toast(`Стиль кнопок: ${BUTTON_STYLES.find(b => b.id === styleId)?.name || styleId}`, "ok");
  };

  const [activeLayout, setActiveLayout] = useState<string>(() => {
    try {
      return localStorage.getItem("nano:layout_preset") || "classic";
    } catch {
      return "classic";
    }
  });

  const [toolbarButtons, setToolbarButtons] = useState<Record<string, boolean>>(() => {
    try {
      const saved = localStorage.getItem("nano:toolbar_buttons");
      return saved ? JSON.parse(saved) : {
        search: true,
        export: true,
        diff: true,
        terminal: true,
        ota: true,
        bugreport: true,
      };
    } catch {
      return { search: true, export: true, diff: true, terminal: true, ota: true, bugreport: true };
    }
  });

  const toggleToolbarButton = (key: string) => {
    setToolbarButtons(prev => {
      const next = { ...prev, [key]: !prev[key] };
      try {
        localStorage.setItem("nano:toolbar_buttons", JSON.stringify(next));
        window.dispatchEvent(new CustomEvent("nano:toolbar-updated", { detail: next }));
      } catch {}
      return next;
    });
  };

  const selectLayout = (layoutId: string) => {
    setActiveLayout(layoutId);
    try {
      localStorage.setItem("nano:layout_preset", layoutId);
      document.documentElement.setAttribute("data-layout-preset", layoutId);
    } catch {}
    toast(`Выбрана компоновка: ${LAYOUT_PRESETS.find(p => p.id === layoutId)?.name || layoutId}`, "ok");
  };

  const saveCustomTheme = () => {
    const newTheme: Theme = {
      id: `custom-theme-${editingSlot + 1}`,
      name: themeDraftName || `Кастомная тема ${editingSlot + 1}`,
      description: "Пользовательская тема NanoDecompiler",
      author: "NanoDev User",
      isCustom: true,
      colors: {
        bg: themeDraftBg,
        surface: themeDraftSurface,
        raised: themeDraftSurface,
        overlay: themeDraftSurface,
        line: "#263028",
        lineStrong: "#3b4a3e",
        ink: "#f1f5f9",
        dim: "#94a3b8",
        faint: "#64748b",
        acid: themeDraftAcid,
        warn: "#f59e0b",
        err: "#ef4444",
        tokK: themeDraftTokK,
        tokS: themeDraftTokS,
        tokC: "#6b7280",
        tokA: "#c084fc",
        tokN: "#fb923c",
      },
    };

    const updated = [...customThemes];
    updated[editingSlot] = newTheme;
    if (updated.length > 2) updated.splice(2);
    setCustomThemes(updated);
    try {
      localStorage.setItem("nano:custom_themes", JSON.stringify(updated));
    } catch {}

    setDraft((d: any) => ({ ...d, themeId: newTheme.id }));
    applyTheme(newTheme);
    toast(`Тема «${newTheme.name}» успешно сохранена и применена!`, "ok");
  };

  const refreshLangs = () => {
    if (window.nano?.getCustomTranslations) {
      window.nano.getCustomTranslations().then(trans => {
        if (trans) {
          for (const [k, v] of Object.entries(trans)) {
            registerCustomTranslation(k, (v as any).name, (v as any).dict);
          }
        }
        setAvailableLangs(getAvailableLanguages());
      }).catch(() => setAvailableLangs(getAvailableLanguages()));
    } else {
      setAvailableLangs(getAvailableLanguages());
    }
  };

  useEffect(() => {
    refreshLangs();
  }, []);

  const [editorFont, setEditorFont] = useState<string>(() => {
    try {
      return localStorage.getItem("nano:editor_font") || "jetbrains";
    } catch {
      return "jetbrains";
    }
  });

  const [editorZoom, setEditorZoom] = useState(() => {
    try {
      const s = localStorage.getItem("nano:editor_zoom");
      return s ? Number(s) || 100 : 100;
    } catch {
      return 100;
    }
  });

  const selectFont = (fontId: string) => {
    setEditorFont(fontId);
    try {
      localStorage.setItem("nano:editor_font", fontId);
      const found = EDITOR_FONTS.find(f => f.id === fontId);
      if (found) {
        document.documentElement.style.setProperty("--font-code", found.fontFamily);
      }
    } catch {}
  };

  const updateEditorZoom = (val: number) => {
    const clamped = Math.max(50, Math.min(250, val));
    setEditorZoom(clamped);
    try {
      localStorage.setItem("nano:editor_zoom", String(clamped));
      document.documentElement.style.setProperty("--editor-zoom", `${clamped}%`);
    } catch {}
  };

  const selectedFontObj = EDITOR_FONTS.find(f => f.id === editorFont) || EDITOR_FONTS[0];

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm p-4"
      onClick={e => {
        if (e.target === e.currentTarget) setSettingsOpen(false);
      }}
    >
      <div
        role="dialog"
        aria-modal="true"
        aria-label="Настройки"
        className="animate-rise flex max-h-[88vh] w-[640px] flex-col overflow-hidden rounded-2xl border border-line bg-surface shadow-2xl shadow-black/50"
      >
        <div className="flex h-11 flex-none items-center gap-2 border-b border-line px-4">
          <h2 className="text-[13px] font-semibold text-ink">Настройки</h2>
          <div className="flex-1" />
          <Kbd>Esc</Kbd>
          <button className="icon-btn h-7 w-7" onClick={() => setSettingsOpen(false)} aria-label="Закрыть">
            <X size={14} />
          </button>
        </div>

        <div className="flex h-9 flex-none items-center gap-1 border-b border-line px-3 overflow-x-auto">
          {([
            { key: "general", label: t(draft.language, "settings.tab.general") },
            { key: "themes", label: "Темы оформления" },
            { key: "layout", label: "Темы положений [БЕТА]" },
            { key: "decompiler", label: t(draft.language, "settings.tab.decompiler") },
            { key: "editor", label: t(draft.language, "settings.tab.editor") },
            { key: "about", label: t(draft.language, "settings.tab.about") },
          ] as const).map(item => (
            <button
              key={item.key}
              className={cn(
                "rounded-md px-2.5 py-1 text-[11.5px] transition-colors whitespace-nowrap",
                tab === item.key ? "bg-acid/10 text-acid font-medium" : "text-faint hover:text-ink",
              )}
              onClick={() => setTab(item.key)}
            >
              {item.label}
            </button>
          ))}
        </div>

        {/* ВКЛАДКА 1: ОБЩИЕ */}
        {tab === "general" && (
          <div className="min-h-0 flex-1 overflow-y-auto px-4 py-3">
            <p className="kicker pt-1 pb-2">{t(draft.language, "settings.section.language")}</p>
            <div className="mb-4 rounded-xl border border-line bg-bg px-3 py-2.5">
              <div className="flex flex-col gap-2">
                <div className="flex items-center justify-between">
                  <div>
                    <p className="text-[12.5px] text-ink/90">Язык интерфейса</p>
                    <p className="mt-0.5 text-[11px] leading-snug text-faint">
                      Разверните список для выбора доступных языков и пользовательских локализаций
                    </p>
                  </div>
                  <div className="flex items-center gap-1.5">
                    <button
                      type="button"
                      className="btn btn-tonal h-6 gap-1 px-2 text-[10.5px]"
                      title="Создать шаблон своего перевода (custom_template.json)"
                      onClick={() => {
                        window.nano.createTranslationTemplate().then(res => {
                          if (res.ok) {
                            toast("Шаблон перевода создан в папке переводов!", "ok");
                            refreshLangs();
                          } else {
                            toast("Не удалось создать шаблон", "err");
                          }
                        }).catch(() => toast("Ошибка создания шаблона", "err"));
                      }}
                    >
                      <Globe size={11} />
                      Свой язык
                    </button>
                    <button
                      type="button"
                      className="btn btn-tonal h-6 gap-1 px-2 text-[10.5px]"
                      title="Открыть папку с файлами локализации"
                      onClick={() => window.nano.openTranslationsFolder()}
                    >
                      <FolderOpen size={11} />
                      Папка
                    </button>
                  </div>
                </div>

                <div className="relative mt-1">
                  <button
                    type="button"
                    onClick={() => {
                      setLangDropdownOpen(v => !v);
                      refreshLangs();
                    }}
                    className="flex w-full items-center justify-between rounded-lg border border-line bg-surface px-3 py-2 text-[12.5px] text-ink transition-colors hover:border-acid/40"
                  >
                    <div className="flex items-center gap-2">
                      <Globe size={14} className="text-acid" />
                      <span className="font-medium">
                        {availableLangs.find(l => l.key === draft.language)?.name || draft.language}
                      </span>
                      <span className="mono rounded bg-line/40 px-1.5 py-0.5 text-[10px] text-faint uppercase">
                        {draft.language}
                      </span>
                    </div>
                    <ChevronDown
                      size={14}
                      className={cn("text-faint transition-transform duration-200", langDropdownOpen && "rotate-180")}
                    />
                  </button>

                  {langDropdownOpen && (
                    <div className="absolute left-0 right-0 top-full z-50 mt-1 max-h-56 overflow-y-auto rounded-lg border border-line bg-surface shadow-xl">
                      <div className="p-1">
                        {availableLangs.map(lang => {
                          const isSelected = draft.language === lang.key;
                          return (
                            <button
                              key={lang.key}
                              type="button"
                              onClick={() => {
                                setDraft((d: any) => ({ ...d, language: lang.key }));
                                setLangDropdownOpen(false);
                              }}
                              className={cn(
                                "flex w-full items-center justify-between rounded-md px-2.5 py-1.5 text-left text-[12px] transition-colors",
                                isSelected
                                  ? "bg-acid/10 text-acid font-medium"
                                  : "text-ink/80 hover:bg-bg hover:text-ink"
                              )}
                            >
                              <div className="flex items-center gap-2">
                                <span>{lang.name}</span>
                                <span className="mono text-[10.5px] text-faint">({lang.key})</span>
                              </div>
                              {isSelected && <Check size={13} className="text-acid" />}
                            </button>
                          );
                        })}
                      </div>
                    </div>
                  )}
                </div>
              </div>
            </div>

            <p className="kicker pt-2 pb-2">Каталоги</p>
            <div className="mb-4 rounded-xl border border-line bg-bg px-3">
              <div className="flex items-center gap-2 py-2.5">
                <div className="min-w-0 flex-1">
                  <p className="text-[12.5px] text-ink/90">Папка для результатов</p>
                  <p className="mono truncate text-[11px] text-faint">{draft.outputDir}</p>
                </div>
                <button
                  className="icon-btn h-8 w-8 border border-line"
                  title="Выбрать другую папку"
                  onClick={() =>
                    window.nano
                      .selectOutDir(draft.outputDir)
                      .then((p: string | null) => {
                        if (p) setDraft((d: any) => ({ ...d, outputDir: p }));
                      })
                      .catch(() => toast("Диалог выбора папки недоступен", "err"))
                  }
                >
                  <FolderOpen size={14} />
                </button>
              </div>
            </div>
          </div>
        )}

        {/* ВКЛАДКА 2: ТЕМЫ ОФОРМЛЕНИЯ И РЕДАКТОР ТЕМ */}
        {tab === "themes" && (
          <div className="min-h-0 flex-1 overflow-y-auto px-4 py-3">
            <div className="flex items-center justify-between pt-1 pb-2">
              <p className="kicker">Встроенные палитры оформления</p>
              <button
                className="btn btn-tonal h-6 gap-1 px-2 text-[11px]"
                onClick={() => window.nano.openThemesFile()}
              >
                <FileCode2 size={12} />
                themes.json
              </button>
            </div>

            <div className="grid grid-cols-2 gap-2.5 mb-4">
              {[...BUILTIN_THEMES, ...customThemes].map(th => {
                const isSelected = (draft.themeId || "terminal-noir") === th.id;
                return (
                  <div
                    key={th.id}
                    onClick={() => {
                      setDraft((d: any) => ({ ...d, themeId: th.id }));
                      applyTheme(th);
                    }}
                    className={cn(
                      "cursor-pointer rounded-xl border p-3 transition-all relative",
                      isSelected
                        ? "border-acid/60 bg-acid/5 shadow-sm shadow-acid/10"
                        : "border-line bg-bg hover:border-line-strong hover:bg-surface",
                    )}
                  >
                    <div className="flex items-center justify-between mb-1.5">
                      <span className="text-[12.5px] font-medium text-ink flex items-center gap-1.5">
                        {th.name}
                        {th.isCustom && <span className="chip border-acid/40 text-acid text-[9.5px]">КАСТОМ</span>}
                      </span>
                      {isSelected && (
                        <span className="flex h-4 w-4 items-center justify-center rounded-full bg-acid text-bg text-[10px]">
                          <Check size={10} strokeWidth={3} />
                        </span>
                      )}
                    </div>
                    {th.description && (
                      <p className="text-[11px] leading-tight text-faint mb-2.5 line-clamp-1">{th.description}</p>
                    )}
                    <div className="flex items-center gap-1.5 pt-1 border-t border-line/40">
                      <span className="h-4 w-4 rounded-full border border-white/10" style={{ backgroundColor: th.colors.bg }} title="Фон" />
                      <span className="h-4 w-4 rounded-full border border-white/10" style={{ backgroundColor: th.colors.surface }} title="Панели" />
                      <span className="h-4 w-4 rounded-full border border-white/10" style={{ backgroundColor: th.colors.acid }} title="Акцент" />
                      <span className="h-4 w-4 rounded-full border border-white/10" style={{ backgroundColor: th.colors.tokK }} title="Синтаксис" />
                    </div>
                  </div>
                );
              })}
            </div>

            {/* ПОЛНОЦЕННЫЙ РЕДАКТОР КАСТОМНЫХ ТЕМ (ДО 2 СЛОТОВ) */}
            <div className="rounded-xl border border-line bg-bg p-3.5 mb-2">
              <div className="flex items-center justify-between pb-2 mb-2 border-b border-line">
                <div className="flex items-center gap-2">
                  <Paintbrush size={14} className="text-acid" />
                  <p className="text-[12.5px] font-semibold text-ink">Редактор тем (до 2 кастомных тем)</p>
                </div>
                <div className="flex items-center gap-1">
                  {[0, 1].map(slot => (
                    <button
                      key={slot}
                      className={cn(
                        "rounded px-2 py-0.5 text-[11px] font-mono transition-colors",
                        editingSlot === slot ? "bg-acid text-bg font-bold" : "bg-surface border border-line text-faint"
                      )}
                      onClick={() => {
                        setEditingSlot(slot);
                        if (customThemes[slot]) {
                          const t = customThemes[slot];
                          setThemeDraftName(t.name);
                          setThemeDraftBg(t.colors.bg);
                          setThemeDraftSurface(t.colors.surface);
                          setThemeDraftAcid(t.colors.acid);
                          setThemeDraftTokK(t.colors.tokK);
                          setThemeDraftTokS(t.colors.tokS);
                        } else {
                          setThemeDraftName(`Моя тема ${slot + 1}`);
                        }
                      }}
                    >
                      Слот {slot + 1}
                    </button>
                  ))}
                </div>
              </div>

              <div className="grid grid-cols-2 gap-3 text-[11.5px]">
                <div>
                  <label className="text-faint block mb-1">Название темы</label>
                  <input
                    type="text"
                    value={themeDraftName}
                    onChange={e => setThemeDraftName(e.target.value)}
                    className="w-full rounded-lg border border-line bg-surface px-2.5 py-1.5 text-ink outline-none focus:border-acid/60"
                  />
                </div>
                <div className="flex items-center gap-2 pt-5">
                  <button
                    className="btn btn-primary h-8 flex-1 text-[11.5px] gap-1.5"
                    onClick={saveCustomTheme}
                  >
                    <Check size={13} />
                    Сохранить тему в слот {editingSlot + 1}
                  </button>
                </div>

                <div className="flex items-center justify-between rounded-lg border border-line bg-surface p-2">
                  <span className="text-ink">Цвет фона (bg):</span>
                  <input type="color" value={themeDraftBg} onChange={e => setThemeDraftBg(e.target.value)} className="h-6 w-8 cursor-pointer rounded border-0 bg-transparent" />
                </div>
                <div className="flex items-center justify-between rounded-lg border border-line bg-surface p-2">
                  <span className="text-ink">Цвет панелей (surface):</span>
                  <input type="color" value={themeDraftSurface} onChange={e => setThemeDraftSurface(e.target.value)} className="h-6 w-8 cursor-pointer rounded border-0 bg-transparent" />
                </div>
                <div className="flex items-center justify-between rounded-lg border border-line bg-surface p-2">
                  <span className="text-ink">Акцентный цвет (acid):</span>
                  <input type="color" value={themeDraftAcid} onChange={e => setThemeDraftAcid(e.target.value)} className="h-6 w-8 cursor-pointer rounded border-0 bg-transparent" />
                </div>
                <div className="flex items-center justify-between rounded-lg border border-line bg-surface p-2">
                  <span className="text-ink">Ключевые слова (Java):</span>
                  <input type="color" value={themeDraftTokK} onChange={e => setThemeDraftTokK(e.target.value)} className="h-6 w-8 cursor-pointer rounded border-0 bg-transparent" />
                </div>
              </div>
            </div>
          </div>
        )}

        {/* ВКЛАДКА 3: ТЕМЫ ПОЛОЖЕНИЙ И СТИЛЕЙ КНОПОК */}
        {tab === "layout" && (
          <div className="min-h-0 flex-1 overflow-y-auto px-4 py-3">
            {/* ГЛОБАЛЬНЫЕ ТЕМЫ КНОПОК */}
            <div className="flex items-center gap-2 pt-1 pb-2">
              <Sliders size={15} className="text-acid" />
              <p className="kicker">Глобальный стиль кнопок (UI Theme)</p>
            </div>
            <p className="text-[11.5px] text-faint mb-3">
              Мгновенно меняет форму, неоновые акценты и эффекты всех кнопок в интерфейсе.
            </p>

            <div className="grid grid-cols-2 gap-2.5 mb-5">
              {BUTTON_STYLES.map(bs => {
                const active = buttonStyle === bs.id;
                return (
                  <button
                    key={bs.id}
                    onClick={() => selectButtonStyle(bs.id)}
                    className={cn(
                      "rounded-xl border p-3 text-left transition-all relative",
                      active ? "border-acid/60 bg-acid/5 shadow-sm shadow-acid/10" : "border-line bg-bg hover:border-line-strong hover:bg-surface"
                    )}
                  >
                    <div className="flex items-center justify-between mb-1">
                      <span className="text-[13px] font-semibold text-ink flex items-center gap-1.5">
                        {bs.name}
                      </span>
                      {active && (
                        <span className="flex h-4 w-4 items-center justify-center rounded-full bg-acid text-bg text-[10px]">
                          <Check size={10} strokeWidth={3} />
                        </span>
                      )}
                    </div>
                    <p className="text-[11px] text-faint leading-tight mb-2">{bs.desc}</p>
                    <div className="pt-1">
                      <span className="btn btn-tonal text-[10.5px] py-0.5 px-2 pointer-events-none inline-block">
                        Образец кнопки
                      </span>
                    </div>
                  </button>
                );
              })}
            </div>

            <div className="flex items-center gap-2 pt-1 pb-2 border-t border-line">
              <Layout size={15} className="text-acid" />
              <p className="kicker">Темы положений и компоновки (БЕТА)</p>
            </div>
            <p className="text-[11.5px] text-faint mb-3">
              Глобально меняет структуру окон и взаимное расположение рабочих блоков (дерево, код, терминал).
            </p>

            <div className="grid grid-cols-2 gap-2.5 mb-4">
              {LAYOUT_PRESETS.map(p => {
                const active = activeLayout === p.id;
                return (
                  <button
                    key={p.id}
                    onClick={() => selectLayout(p.id)}
                    className={cn(
                      "rounded-xl border p-3 text-left transition-all relative",
                      active ? "border-acid/60 bg-acid/5 shadow-sm shadow-acid/10" : "border-line bg-bg hover:border-line-strong hover:bg-surface"
                    )}
                  >
                    <div className="flex items-center justify-between mb-1">
                      <span className="text-[13px] font-semibold text-ink flex items-center gap-1.5">
                        <span>{p.icon}</span>
                        {p.name}
                      </span>
                      {active && (
                        <span className="flex h-4 w-4 items-center justify-center rounded-full bg-acid text-bg text-[10px]">
                          <Check size={10} strokeWidth={3} />
                        </span>
                      )}
                    </div>
                    <p className="text-[11px] text-faint leading-tight">{p.desc}</p>
                  </button>
                );
              })}
            </div>

            <div className="rounded-xl border border-line bg-bg p-3.5">
              <div className="flex items-center gap-2 pb-2 mb-2 border-b border-line">
                <Sliders size={14} className="text-acid" />
                <p className="text-[12.5px] font-semibold text-ink">Настройка кнопок на панели управления</p>
              </div>
              <div className="grid grid-cols-2 gap-2 text-[12px]">
                <label className="flex items-center gap-2 cursor-pointer p-1.5 rounded hover:bg-surface">
                  <input type="checkbox" checked={!!toolbarButtons.search} onChange={() => toggleToolbarButton("search")} className="rounded border-line text-acid" />
                  <span className="text-ink">Поиск по проекту (Ctrl+Shift+F)</span>
                </label>
                <label className="flex items-center gap-2 cursor-pointer p-1.5 rounded hover:bg-surface">
                  <input type="checkbox" checked={!!toolbarButtons.export} onChange={() => toggleToolbarButton("export")} className="rounded border-line text-acid" />
                  <span className="text-ink">Экспорт проекта в ZIP</span>
                </label>
                <label className="flex items-center gap-2 cursor-pointer p-1.5 rounded hover:bg-surface">
                  <input type="checkbox" checked={!!toolbarButtons.diff} onChange={() => toggleToolbarButton("diff")} className="rounded border-line text-acid" />
                  <span className="text-ink">Сравнение версий (Diff)</span>
                </label>
                <label className="flex items-center gap-2 cursor-pointer p-1.5 rounded hover:bg-surface">
                  <input type="checkbox" checked={!!toolbarButtons.terminal} onChange={() => toggleToolbarButton("terminal")} className="rounded border-line text-acid" />
                  <span className="text-ink">Терминал логов</span>
                </label>
                <label className="flex items-center gap-2 cursor-pointer p-1.5 rounded hover:bg-surface">
                  <input type="checkbox" checked={!!toolbarButtons.ota} onChange={() => toggleToolbarButton("ota")} className="rounded border-line text-acid" />
                  <span className="text-ink">OTA Обновления</span>
                </label>
                <label className="flex items-center gap-2 cursor-pointer p-1.5 rounded hover:bg-surface">
                  <input type="checkbox" checked={!!toolbarButtons.bugreport} onChange={() => toggleToolbarButton("bugreport")} className="rounded border-line text-acid" />
                  <span className="text-ink">Быстрый баг-репорт</span>
                </label>
              </div>
            </div>
          </div>
        )}

        {/* ВКЛАДКА 4: ДЕКОМПИЛЯТОР */}
        {tab === "decompiler" && (
          <div className="min-h-0 flex-1 overflow-y-auto px-4 py-3">
            <p className="kicker pt-1 pb-2">Параметры C++ ядра</p>
            <div className="mb-4 rounded-xl border border-line bg-bg px-3">
              <Row
                label="Детальная информация о плагине"
                hint="Отображать подробные данные байткода, токенов и скрытых маркеров в дереве плагина (по умолчанию выключено)"
                control={
                  <Toggle
                    label="Детальная информация о плагине"
                    checked={draft.detailedPluginInfo ?? false}
                    onChange={v => setDraft((d: any) => ({ ...d, detailedPluginInfo: v }))}
                  />
                }
              />
              <div className="h-px bg-line" />
              <Row
                label="Потоки движка"
                hint="параллельный разбор классов в C++ ядре"
                control={
                  <div className="flex items-center gap-1.5">
                    <button
                      className="icon-btn h-7 w-7 border border-line"
                      onClick={() => setDraft((d: any) => ({ ...d, threads: Math.max(1, d.threads - 1) }))}
                      aria-label="Меньше"
                    >
                      <Minus size={12} />
                    </button>
                    <span className="mono w-8 text-center text-[13px] text-ink tabular-nums">{draft.threads}</span>
                    <button
                      className="icon-btn h-7 w-7 border border-line"
                      onClick={() => setDraft((d: any) => ({ ...d, threads: Math.min(16, d.threads + 1) }))}
                      aria-label="Больше"
                    >
                      <Plus size={12} />
                    </button>
                  </div>
                }
              />
              <div className="h-px bg-line" />
              <Row
                label="Переименовывать обфусцированные члены"
                hint="эвристика по сигнатурам и омоглифам; ниже порога имена остаются как есть"
                control={
                  <Toggle
                    label="Переименовывать обфусцированные члены"
                    checked={draft.renameObfuscated}
                    onChange={v => setDraft((d: any) => ({ ...d, renameObfuscated: v }))}
                  />
                }
              />
              <div className="h-px bg-line" />
              <Row
                label="Сохранять номера строк байткода"
                hint="комментарии /* line: n */ — помогает сверять со стектрейсами"
                control={
                  <Toggle
                    label="Сохранять номера строк байткода"
                    checked={draft.keepLineNumbers}
                    onChange={v => setDraft((d: any) => ({ ...d, keepLineNumbers: v }))}
                  />
                }
              />
              <div className="h-px bg-line" />
              <Row
                label="Открывать папку по завершении"
                control={
                  <Toggle
                    label="Открывать папку по завершении"
                    checked={draft.openFolderOnDone}
                    onChange={v => setDraft((d: any) => ({ ...d, openFolderOnDone: v }))}
                  />
                }
              />
              <div className="h-px bg-line" />
              <Row
                label="Отправка отчётов и ошибок разработчику"
                hint="Включено по умолчанию для автоматической диагностики зависаний и крашей через Telegram-сервер"
                control={
                  <Toggle
                    label="Отправка отчётов разработчику"
                    checked={draft.telemetryEnabled ?? true}
                    onChange={v => setDraft((d: any) => ({ ...d, telemetryEnabled: v }))}
                  />
                }
              />
            </div>
          </div>
        )}

        {/* ВКЛАДКА 5: РЕДАКТОР И ШРИФТЫ */}
        {tab === "editor" && (
          <div className="min-h-0 flex-1 overflow-y-auto px-4 py-3">
            <p className="kicker pt-1 pb-2">Масштаб просмотра кода</p>
            <div className="mb-4 rounded-xl border border-line bg-bg px-3">
              <Row
                label="Масштаб шрифта"
                hint="Базовый размер текста в окне просмотра кода (Ctrl + / Ctrl -)"
                control={
                  <div className="flex items-center gap-1.5">
                    <button
                      className="icon-btn h-7 w-7 border border-line"
                      onClick={() => updateEditorZoom(editorZoom - 10)}
                    >
                      <Minus size={13} />
                    </button>
                    <span className="mono w-12 text-center text-[12px] text-ink">{editorZoom}%</span>
                    <button
                      className="icon-btn h-7 w-7 border border-line"
                      onClick={() => updateEditorZoom(editorZoom + 10)}
                    >
                      <Plus size={13} />
                    </button>
                  </div>
                }
              />
            </div>

            <p className="kicker pt-2 pb-2">Шрифт кода (Моноширинный)</p>
            <div className="mb-4 space-y-2">
              {EDITOR_FONTS.map(f => {
                const active = editorFont === f.id;
                return (
                  <button
                    key={f.id}
                    onClick={() => selectFont(f.id)}
                    className={cn(
                      "w-full text-left rounded-xl border p-3 transition-colors",
                      active ? "border-acid/50 bg-acid/5" : "border-line bg-bg hover:border-line-strong",
                    )}
                  >
                    <div className="flex items-center justify-between">
                      <span className="text-[13px] font-medium text-ink" style={{ fontFamily: f.fontFamily }}>
                        {f.label}
                      </span>
                      {active && (
                        <span className="chip border-acid/35 text-acid text-[10.5px]">
                          <span className="dot bg-acid" /> активен
                        </span>
                      )}
                    </div>
                    <p className="mt-1 text-[11px] text-faint leading-snug">{f.desc}</p>
                  </button>
                );
              })}
            </div>

            {/* ПРЕДПРОСМОТР КОДА С НАСТОЯЩЕЙ ПОДСВЕТКОЙ СИНТАКСИСА */}
            <p className="kicker pt-2 pb-2">Предпросмотр кода</p>
            <div className="mb-4 rounded-xl border border-line bg-bg p-3 shadow-inner">
              <pre className="mono text-[11.5px] leading-relaxed overflow-x-auto" style={{ fontFamily: selectedFontObj.fontFamily }}>
                <div><span className="text-faint italic">// Пример декомпиляции с текущим шрифтом и темой</span></div>
                <div><span className="text-purple-400 font-bold">package</span> <span className="text-ink">dev.nanodev.aegis</span>;</div>
                <div>&nbsp;</div>
                <div><span className="text-purple-400 font-bold">public class</span> <span className="text-yellow-300 font-bold">NanoDecompiledPlugin</span> <span className="text-purple-400 font-bold">extends</span> <span className="text-yellow-300">JavaPlugin</span> &#123;</div>
                <div>&nbsp;&nbsp;&nbsp;&nbsp;<span className="text-yellow-500/90 font-medium">@Override</span></div>
                <div>&nbsp;&nbsp;&nbsp;&nbsp;<span className="text-purple-400 font-bold">public void</span> <span className="text-blue-400 font-semibold">onEnable</span>() &#123;</div>
                <div>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<span className="text-blue-300">getLogger</span>().<span className="text-blue-300">info</span>(<span className="text-green-400">"&sect;a&#10004; Плагин успешно инициализирован без обфускации"</span>);</div>
                <div>&nbsp;&nbsp;&nbsp;&nbsp;&#125;</div>
                <div>&#125;</div>
              </pre>
            </div>
          </div>
        )}

        {/* ВКЛАДКА 6: О ПРОГРАММЕ */}
        {tab === "about" && (
          <div className="min-h-0 flex-1 overflow-y-auto px-4 py-3">
            <div className="mb-4 rounded-xl border border-line bg-bg p-4 text-center">
              <div className="mx-auto mb-2 grid h-12 w-12 place-items-center rounded-2xl border border-acid/40 bg-acid/10 text-acid shadow-lg shadow-acid/10">
                <Sparkles size={22} />
              </div>
              <h3 className="text-[15px] font-bold text-ink">NanoDecompiler</h3>
              <p className="mt-0.5 text-[12px] text-faint">
                {formatVersionsDisplay(guiVersion || "2.0.2", engineVersion || "2.0.2")}
              </p>
            </div>

            <p className="kicker pb-2">Разработчик</p>
            <div className="space-y-2 mb-4">
              <TelegramCredit handle="WineSquad" role="Официальный канал WineSquad" />
              <TelegramCredit handle="radoqi" role="NanoDev (связь / предложения)" />
            </div>
          </div>
        )}

        <div className="flex h-12 flex-none items-center justify-between border-t border-line bg-surface px-4">
          <div className="flex items-center gap-2">
            <button
              className="btn btn-tonal h-7 text-[11.5px]"
              onClick={() => {
                setDraft(settings);
                setSettingsOpen(false);
              }}
            >
              Отмена
            </button>
          </div>
          <button
            className="btn btn-primary h-7 text-[11.5px]"
            onClick={async () => {
              await saveSettings(draft);
              toast("Настройки сохранены", "ok");
              setSettingsOpen(false);
            }}
          >
            Сохранить
          </button>
        </div>
      </div>
    </div>
  );
}
