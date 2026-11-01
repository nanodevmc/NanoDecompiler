import { memo, useMemo, type ReactNode } from "react";
import { MC_CODE_RE, renderMcColored } from "./mcColors";

/* БАГ-ФИКС: раньше ЛЮБОЙ файл в мини-просмотрщике проходил через
   JavaCode (java-специфичный токенизатор) - plugin.yml/pom.xml
   подсвечивались так, будто это Java (ключевые слова public/class и
   т.п. подсвечивались бы там, где их и близко нет, а реальный
   YAML/XML-синтаксис - нет). Здесь - отдельные лёгкие токенизаторы под
   YAML/properties и XML, тот же визуальный каркас (номера строк,
   моноширинный шрифт), что и у JavaCode, для единообразия. */

interface Token {
  text: string;
  cls: string | null;
}

function renderTokens(tokens: Token[], key: number): ReactNode {
  return (
    <span key={key}>
      {tokens.map((t, i) =>
        // НОВОЕ v1.8.0: цвета Minecraft ищем ВО ВСЕХ токенах (не только
        // cls="tok-s") - значения .properties обычно БЕЗ кавычек
        // (`msg=&aПривет`), такой текст вообще не получает cls в
        // существующих токенизаторах ниже, но именно там чаще всего и
        // живут реальные цветовые коды. Пропускаем только сами комментарии
        // (tok-c) - код внутри "# закомментированного" текста красить не
        // нужно, это не то, что реально отправится игроку.
        t.cls !== "tok-c" && MC_CODE_RE.test(t.text) ? (
          <span key={i} className={t.cls ?? undefined}>
            {renderMcColored(t.text)}
          </span>
        ) : t.cls ? (
          <span key={i} className={t.cls}>
            {t.text}
          </span>
        ) : (
          <span key={i}>{t.text}</span>
        ),
      )}
    </span>
  );
}

// YAML / конфигурации: комментарии (#), ключи (любой язык включая кириллицу, кавычки),
// значения (числа, булевы, кавычки и неэкранированные строки с цветами Minecraft)
const YAML_LINE_RE = /^(\s*)((?:#|!)[^\n]*)?$/;
// Поддерживает кириллические ключи (настройки:, мир:), спецсимволы, кавычки и дефисы списков
const YAML_KEY_RE = /^(\s*(?:-\s*)?)(["'][^"'\n]+["']|[^\s:#][^:#\n]*?)(\s*:)(.*)$/;
const YAML_TOKEN_RE = new RegExp(
  [
    String.raw`((?:#|!)[^\n]*)`,
    String.raw`("(?:[^"\\\n]|\\.)*")`,
    String.raw`('(?:[^'\\\n]|\\.)*')`,
    String.raw`\b(true|false|null|yes|no|on|off)\b`,
    String.raw`\b(-?\d[\d.]*)\b`,
  ].join("|"),
  "gi",
);

function tokenizeYamlLine(line: string): Token[] {
  const commentOnly = YAML_LINE_RE.exec(line);
  if (commentOnly && commentOnly[2]) {
    const indent = commentOnly[1];
    return indent ? [{ text: indent, cls: null }, { text: commentOnly[2], cls: "tok-c" }] : [{ text: commentOnly[2], cls: "tok-c" }];
  }
  const keyMatch = YAML_KEY_RE.exec(line);
  const out: Token[] = [];
  let rest = line;
  if (keyMatch) {
    out.push({ text: keyMatch[1], cls: null }, { text: keyMatch[2], cls: "tok-a font-semibold" }, { text: keyMatch[3], cls: "tok-p" });
    rest = keyMatch[4];
  }
  
  if (!rest.trim()) return out;

  let last = 0;
  YAML_TOKEN_RE.lastIndex = 0;
  let m: RegExpExecArray | null;
  let matchedAny = false;
  while ((m = YAML_TOKEN_RE.exec(rest)) !== null) {
    matchedAny = true;
    if (m.index > last) {
      const between = rest.slice(last, m.index);
      // Если между токенами есть непустой текст значения (unquoted string) - красим как строку!
      const cls = between.trim() ? "tok-s" : null;
      out.push({ text: between, cls });
    }
    const text = m[0];
    let cls: string | null = "tok-s";
    if (text.startsWith("#") || text.startsWith("!")) {
      cls = "tok-c";
    } else if (/^(true|false|null|yes|no|on|off)$/i.test(text)) {
      cls = "tok-k font-semibold";
    } else if (/^-?\d[\d.]*$/.test(text)) {
      cls = "tok-m";
    }
    out.push({ text, cls });
    last = m.index + text.length;
  }
  if (last < rest.length) {
    const trailing = rest.slice(last);
    // Остаток значения строки красим как tok-s (строка/значение), чтобы ничего не оставалось белым!
    const cls = trailing.trim() ? "tok-s" : null;
    out.push({ text: trailing, cls });
  } else if (!matchedAny && rest.trim()) {
    // Вся строка - неэкранированное значение/строка
    out.push({ text: rest, cls: "tok-s" });
  }
  return out;
}

// Служебные маппинги (MAPPING_RU.TXT, mapping.txt, deobf_mapping.txt):
// строка: `a.b.c -> com.package.Class` или `field_123 -> myField`
function tokenizeMappingLine(line: string): Token[] {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith("#") || trimmed.startsWith("//")) {
    return [{ text: line, cls: "tok-c" }];
  }
  const arrowIdx = line.indexOf("->");
  if (arrowIdx !== -1) {
    const left = line.slice(0, arrowIdx);
    const arrow = "->";
    const right = line.slice(arrowIdx + 2);
    return [
      { text: left, cls: "tok-c font-mono" },
      { text: arrow, cls: "tok-a font-bold" },
      { text: right, cls: "tok-k font-semibold font-mono" },
    ];
  }
  return [{ text: line, cls: "tok-ink/80 font-mono" }];
}

function tokenizePropertiesLine(line: string): Token[] {
  const commentOnly = YAML_LINE_RE.exec(line);
  if (commentOnly && commentOnly[2]) {
    const indent = commentOnly[1];
    return indent ? [{ text: indent, cls: null }, { text: commentOnly[2], cls: "tok-c" }] : [{ text: commentOnly[2], cls: "tok-c" }];
  }
  const keyMatch = PROPERTIES_KEY_RE.exec(line);
  const out: Token[] = [];
  let rest = line;
  if (keyMatch) {
    out.push({ text: keyMatch[1], cls: null }, { text: keyMatch[2], cls: "tok-a" }, { text: keyMatch[3], cls: null });
    rest = keyMatch[4];
  }
  let last = 0;
  YAML_TOKEN_RE.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = YAML_TOKEN_RE.exec(rest)) !== null) {
    if (m.index > last) out.push({ text: rest.slice(last, m.index), cls: null });
    out.push({ text: m[0], cls: "tok-s" });
    last = m.index + m[0].length;
  }
  if (last < rest.length) out.push({ text: rest.slice(last), cls: null });
  return out;
}

// JSON: "ключ": значение, где значение - строка/число/true/false/null/{/[.
// НОВОЕ v1.7.3 (реальная жалоба - в просмотрщике нет подсветки для .json,
// например fabric.mod.json из ViaVersion) - раньше .json вообще не имел
// своего токенизатора, шёл через PlainCode (только pretty-print без
// цвета, см. CodeView.tsx). Построчный подход, как у остальных
// токенизаторов в этом файле - JSON построчно уже отформатирован
// prettyPrintIfJson() в CodeView.tsx до попадания сюда, так что здесь
// не нужен полноценный рекурсивный парсер, только по-строчная разметка.
const JSON_KEY_RE = /^(\s*)("(?:[^"\\]|\\.)*")(\s*:)(.*)$/;
const JSON_VALUE_TOKEN_RE = new RegExp(
  [String.raw`("(?:[^"\\]|\\.)*")`, String.raw`\b(true|false|null)\b`, String.raw`(-?\d[\d.eE+-]*)\b`].join("|"),
  "g",
);

function tokenizeJsonValuePart(text: string): Token[] {
  const out: Token[] = [];
  let last = 0;
  JSON_VALUE_TOKEN_RE.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = JSON_VALUE_TOKEN_RE.exec(text)) !== null) {
    if (m.index > last) out.push({ text: text.slice(last, m.index), cls: null });
    out.push({ text: m[0], cls: "tok-s" });
    last = m.index + m[0].length;
  }
  if (last < text.length) out.push({ text: text.slice(last), cls: null });
  return out;
}

function tokenizeJsonLine(line: string): Token[] {
  const keyMatch = JSON_KEY_RE.exec(line);
  if (keyMatch) {
    const out: Token[] = [
      { text: keyMatch[1], cls: null },
      { text: keyMatch[2], cls: "tok-a" },
      { text: keyMatch[3], cls: null },
    ];
    out.push(...tokenizeJsonValuePart(keyMatch[4]));
    return out;
  }
  // строка без ключа - элемент массива или просто "{"/"}"/"["/"]" - красим
  // только сами значения (строки/числа/true/false/null), остальное как есть.
  return tokenizeJsonValuePart(line);
}

// XML: расширенный четкий токенизатор: пролог (<?xml...?>), CDATA, теги, скобки, атрибуты, строки и entity (&amp;)
const XML_TOKEN_RE = new RegExp(
  [
    String.raw`(<!--[\s\S]*?-->)`,
    String.raw`(<!\[CDATA\[[\s\S]*?\]\]>)`,
    String.raw`(<!DOCTYPE[\s\S]*?>)`,
    String.raw`(<\?[A-Za-z][\w:.\-]*|\?>)`,
    String.raw`(<\/?[A-Za-z_:][\w:.\-]*)`,
    String.raw`(\/?>)`,
    String.raw`("(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*')`,
    String.raw`\b([A-Za-z_:][\w:.\-]*)(?=\s*=)`,
    String.raw`(&[#\w]+;)`,
  ].join("|"),
  "g",
);

function tokenizeXmlLine(line: string): Token[] {
  const out: Token[] = [];
  let last = 0;
  XML_TOKEN_RE.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = XML_TOKEN_RE.exec(line)) !== null) {
    if (m.index > last) {
      const interstitial = line.slice(last, m.index);
      const isWhitespace = /^\s*$/.test(interstitial);
      out.push({ text: interstitial, cls: isWhitespace ? null : "text-ink font-medium" });
    }
    const [, comment, cdata, doctype, pi, tag, close, str, attr, entity] = m;
    if (comment) {
      out.push({ text: m[0], cls: "tok-c" });
    } else if (cdata || doctype) {
      out.push({ text: m[0], cls: "tok-c" });
    } else if (pi) {
      out.push({ text: m[0], cls: "tok-k" });
    } else if (tag) {
      // Split < or </ from tag name for perfect bracket punctuation styling
      const slashIdx = tag.startsWith("</") ? 2 : 1;
      const bracket = tag.slice(0, slashIdx);
      const name = tag.slice(slashIdx);
      out.push({ text: bracket, cls: "tok-punct" });
      out.push({ text: name, cls: "tok-tag" });
    } else if (close) {
      out.push({ text: m[0], cls: "tok-punct" });
    } else if (str) {
      out.push({ text: m[0], cls: "tok-s" });
    } else if (attr) {
      out.push({ text: m[0], cls: "tok-a" });
    } else if (entity) {
      out.push({ text: m[0], cls: "tok-entity" });
    } else {
      out.push({ text: m[0], cls: null });
    }
    last = m.index + m[0].length;
  }
  if (last < line.length) {
    const trailing = line.slice(last);
    const isWhitespace = /^\s*$/.test(trailing);
    out.push({ text: trailing, cls: isWhitespace ? null : "text-ink font-medium" });
  }
  return out;
}

function renderShell(lines: ReactNode[], wrap?: boolean): ReactNode {
  return (
    <>
      {lines.map((row, i) => (
        // НОВОЕ v1.8.0: id на каждой строке - нужен для перехода к
        // результату поиска (см. ProjectSearchModal.tsx/CodeView.tsx) -
        // scrollIntoView ищет ИМЕННО этот id, единый формат для ВСЕХ
        // токенизаторов (не только Java), раз он всё равно все проходят
        // через этот общий renderShell().
        <div key={i} id={`codeline-${i + 1}`} className="flex">
          <span
            aria-hidden
            className="mono w-12 flex-none pr-4 text-right text-[0.88em] leading-[1.75] text-faint select-none"
          >
            {i + 1}
          </span>
          <span
            className={
              "mono text-[1em] leading-[1.75] text-ink/90 " + (wrap ? "whitespace-pre-wrap break-words" : "whitespace-pre")
            }
          >
            {row}
          </span>
        </div>
      ))}
    </>
  );
}

export const YamlCode = memo(function YamlCode({ code, wrap, disableVsCodeLogs }: { code: string; wrap?: boolean; disableVsCodeLogs?: boolean }) {
  const lines = useMemo(() => code.split("\n").map((l, i) => renderTokens(tokenizeYamlLine(l), i)), [code]);
  return renderShell(lines, wrap);
});

export const PropertiesCode = memo(function PropertiesCode({ code, wrap, disableVsCodeLogs }: { code: string; wrap?: boolean; disableVsCodeLogs?: boolean }) {
  const lines = useMemo(() => code.split("\n").map((l, i) => renderTokens(tokenizePropertiesLine(l), i)), [code]);
  return renderShell(lines, wrap);
});

export const JsonCode = memo(function JsonCode({ code, wrap, disableVsCodeLogs }: { code: string; wrap?: boolean; disableVsCodeLogs?: boolean }) {
  const lines = useMemo(() => code.split("\n").map((l, i) => renderTokens(tokenizeJsonLine(l), i)), [code]);
  return renderShell(lines, wrap);
});

export const XmlCode = memo(function XmlCode({ code, wrap, disableVsCodeLogs }: { code: string; wrap?: boolean; disableVsCodeLogs?: boolean }) {
  const lines = useMemo(() => code.split("\n").map((l, i) => renderTokens(tokenizeXmlLine(l), i)), [code]);
  return renderShell(lines, wrap);
});

// Форматы без своего токенизатора (.json/.md/.txt/.gitignore и т.п.) -
// честно показываем как обычный текст без подсветки, а не подделываем под
// java/yaml - неверная подсветка хуже, чем её отсутствие.
export const PlainCode = memo(function PlainCode({ code, wrap, disableVsCodeLogs }: { code: string; wrap?: boolean; disableVsCodeLogs?: boolean }) {
  const lines = useMemo(() => code.split("\n").map((l, i) => <span key={i}>{l}</span>), [code]);
  return renderShell(lines, wrap);
});

export const MappingCode = memo(function MappingCode({ code, wrap }: { code: string; wrap?: boolean }) {
  const lines = useMemo(() => code.split("\n").map((l, i) => renderTokens(tokenizeMappingLine(l), i)), [code]);
  return renderShell(lines, wrap);
});
