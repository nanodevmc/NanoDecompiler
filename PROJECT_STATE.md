# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.5 (Release)
- **Стек**: C++20 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Electron/React (TypeScript, Tailwind, Liquid Glass), Android Kotlin (Material Design 3).
- **Окружение**: Windows 10, слабый процессор Intel Celeron P4600, 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать последовательно.

## Ключевые достижения v2.0.5:
1. **Ликвидация «Черного экрана» в Electron на Windows 11**:
   - Добавлен флаг `disable-gpu-watchdog`, предотвращающий убийство GPU-процесса Chromium при 100% нагрузке на процессор во время декомпиляции.
   - Устранён сбой DWM поверхности swapchain: окно создается скрытым (`show: false`) и плавно отображается по событию `ready-to-show`.
   - Внедрена автоматическая перезагрузка контекста при аварии GPU (`child-process-gone: details.type === 'GPU'`).
   - Добавлена поддержка CLI-флагов аварийного запуска без аппаратного ускорения (`--disable-gpu`, `--safe-mode`).
2. **Синхронизация переменных в замыканиях и лямбдах**:
   - Полная синхронизация слотов и имен захваченных параметров (`binding`, `actor` вместо `var5`/`var6`).
   - Устранены расхождения типов и деградация в `(Object)`.
3. **Человекоподобный чистый код в `emit.cpp` и `structure.cpp`**:
   - Символьные литералы печатаются как `'\\'`, `'\"'`, `'\n'` вместо сырых кодов ASCII 92, 34, 10.
   - Булевы инверсии компилируются в `tgt = !tgt` вместо `tgt = tgt == 0`.
   - Массивы в `toArray` сворачиваются в ссылку на конструктор `String[]::new`.
4. **Рекурсивная зачистка мёртвых синтетических массивов (`__obj1`) и переменных (`temp4`)**:
   - `eliminate_dead_locals` сделан рекурсивным по всем подблокам (`IfStmt`, `WhileStmt`, `BlockStmt`, `TryStmt`).
   - Внедрена связка с `prune_dead_synth_locals`: неиспользуемые синтетические массивы и мертвые записи в них (`__obj1[0] = ...`) удаляются без остатка.
5. **Релизные артефакты v2.0.5**:
   - Мобильный APK: `NanoDecompiler-Mobile-v2.0.5.apk` (4.04 МБ, релизная v2/v3 подпись) -> `https://paste.c-net.org/OmahaOxygen`
   - Копия на Рабочем столе: `C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.5.apk`
   - Нативный Desktop CLI: `resources/engine_cpp/build/NanoDecompilerCLI.exe` (4.05 МБ)
   - Нативный Android CLI: `mobile-android/assets/bin/NanoDecompilerCLI` (2.58 МБ)
   - Нативная ARM64 библиотека: `mobile-android/lib/arm64-v8a/libnanodecompiler_cli.so` (2.58 МБ)
