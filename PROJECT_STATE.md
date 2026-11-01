# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.5 (Release)
- **Стек**: C++20 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Electron/React (TypeScript, Tailwind, Liquid Glass), Android Kotlin (Material Design 3).
- **Окружение**: Windows 10, слабый процессор Intel Celeron P4600, 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать последовательно.

## Ключевые достижения v2.0.5:
1. **Ликвидация «Черного экрана» в Electron на Windows 11**:
   - Устранён нелегальный условный вызов хука `useEngine()` в `CodeView.tsx:205`, вызывавший сбой инвариантов React и размонтирование корня.
   - Внедрён защитный `ErrorBoundary.tsx` в стиле Liquid Glass, предотвращающий падение приложения.
   - Добавлена null-безопасность для `file.pkg.split`, `compareFiles`, `sortTreeNodes`, `localeCompare` в `FileTree.tsx`.
   - В `electron/main.ts` добавлены флаги `disable-gpu-process-crash-limit`, `disable-features: CalculateNativeWinOcclusion` (устранение бага DWM окклюзии окон без рамок на Win 11) и перехват `render-process-gone`.
2. **Ликвидация дублирования CFG и фиктивных `temp3 = 1;` (`structure.cpp`)**:
   - В `build_if_inner` убран ошибочный override точки слияния `merge = forward`, сбивавший непосредственный пост-доминатор.
   - Устранено 3-кратное дублирование кода в `lambda_onPlaceBlock_1` (сокращение с 69 до 28 строк чистого кода).
3. **Умное именование локальных переменных (`structure.cpp`, `emit.cpp`)**:
   - Ликвидированы сырые `var2`, `var3`, `var4`, `var6`, `var7`, `var9`, `var10`, `var11`.
   - Присвоены семантические имена: `query`, `associable`, `flags`, `list`, `isFire`, `isLava`, `flag`, `regions`.
   - Автоматическая свёртка `if (isFire != 0)` в `if (isFire)` и десахаризация булевых операторов.
4. **Мгновенный старт на Android без 15-секундного зависания**:
   - В `FastScanEngine.kt` энтропия Шеннона ограничена выборкой 3 классов вместо 50 (устранён перегрев CPU).
   - В `MainActivity.kt` внедрено буферизованное копирование по 64 КБ с мгновенной отрисовкой статуса.
5. **Релизные артефакты v2.0.5**:
   - Мобильный APK: `NanoDecompiler-Mobile-v2.0.5.apk` (4.03 МБ, релизная v2/v3 подпись) -> `https://paste.c-net.org/DaresIntercom`
   - Копия на Рабочем столе: `C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.5.apk`
   - Нативный Desktop CLI: `resources/engine_cpp/build/NanoDecompilerCLI.exe` (4.05 МБ)
   - Нативный Android CLI: `mobile-android/assets/bin/NanoDecompilerCLI` (2.58 МБ)
   - Нативная ARM64 библиотека: `mobile-android/lib/arm64-v8a/libnanodecompiler_cli.so` (2.58 МБ)
