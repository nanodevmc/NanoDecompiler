# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.2
- **Стек**: C++20 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Electron + React/Vite фронтенд, Avalonia .NET 8 (`NanoDecompilerLite`).
- **Окружение**: Windows 10, слабый процессор, 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать в облаке (GitHub Actions workflow), не грузить ноутбук.
  - Коммиты оформлять через `commit_msg.txt` и `git commit -F commit_msg.txt`.

## Что сделано (v2.0.2 - оптимизация C++ ядра и Java Diff)
- **Ликвидация крашей и квадратичных зависаний C++ ядра (`ElemKits.jar` — 100% успех)**:
  - `structure.cpp`: устранена взаимная рекурсия `simplify_stmt` -> `simplify_stmts` (было $4^{40}$ проходов, вызывало переполнение стека `0xC0000005` на `ElemKits.<init>`). Добавлена отсечка `depth > 60` и `depth > 500` в `Structurer::region`.
  - `emit.cpp`: защита от зацикливания узлов AST (`s_emitting_nodes`), лимит отступов `indent > 45` и глубины выражений `s_expr_depth > 60`. Пиковая память снижена с 1.27 ГБ до 84 МБ.
  - `engine.cpp`: ликвидирован квадратичный/кубический взрыв в `has_escaping_local_decl_check`, `collect_all_escaping_names` и `hoist_escaping_locals`. Алгоритм переведён на линейный проход с конца ($O(N)$) без срезов векторов `rest` и без повторных обходов. Добавлена защита `depth > 50` в обходчиках AST.
  - Результат: `ElemKits.jar` (1121/1121 метод, 84 473 строки) декомпилируется на 100.0% без единого падения за считанные секунды.
- **Интерактивный Java Diff в GUI (`PluginDiffModal.tsx`)**:
  - При клике на изменённый, добавленный или удалённый класс открывается декомпилированный Java-код с номерами строк и diff-подсветкой строк `+` и `-`.
  - В `electron/pluginTools.ts` внедрен LCS diff алгоритм без внешних зависимостей (`computeLineDiff`) и IPC-метод `plugin:diffClass`.
- **Фикс кнопок NanoGuard и дедупликация (`Terminal.tsx`)**:
  - Кнопки «Да, проверить точность» и «Отмена» переведены на React Context `toast` из `useEngine()`.
  - Устранено дублирование алертов детекции в терминале через `lastNanoGuardIdx`.
- **Просмотр нестандартных текстовых ресурсов (`engine.tsx`)**:
  - Разрешён просмотр файлов конфигураций и скриптов: `.aeg`, `.cfg`, `.conf`, `.ini`, `.toml`, `.gradle`, `.kts`, `.env`, `.sh`, `.bat`, `.cmd` и файлов без расширений.

## Что сделано ранее (v2.0.2 / v2.0.1)
- Устранение зацикливания AegisArmorTrap в `structure.cpp`.
- Движок JNI / .dll деобфускации (`jni_deobf.cpp`).
- 25-этапная защитная эвристика NanoGuard (`process_jar.cpp`, `scanner.rs`).
- Центр OTA-обновлений в GUI (`UpdateModal.tsx`).
- Zero Fallback Engine и 10 обновлений по читабельности (ReadUpd 22-31).
