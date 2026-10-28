# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.3 (dev)
- **Стек**: C++20 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Tauri v2 подпапка (`tauri-v2/`), Electron + React/Vite фронтенд, Avalonia .NET 8 (`NanoDecompilerLite`).
- **Окружение**: Windows 10, слабый процессор, 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать последовательно.
  - `test_jars` неприкосновенен для GitHub Actions.

## Что сделано:
1. **Релиз v2.0.2 зафиксирован в git**: коммит `966484b`.
2. **Переход на v2.0.3**:
   - Синхронно поднята версия до `2.0.3` в `package.json`, `src/version.ts`, `tauri-v2/Cargo.toml`, `tauri.conf.json`, `fastscan/Cargo.toml`.
   - Собран финальный установщик `NanoDecompiler Setup 2.0.3.exe` (67.39 МБ) и выложен на рабочий стол.
   - Рабочий стол очищен от старых временных частей (part1, part2) и установщика 2.0.2.
3. **15 уникальных киберпанк/неон анимаций**:
   - `matrix-scanline`, `crt-phosphor-glow`, `neon-pulse`, `radar-sweep`, `quantum-particle`, `dna-wave`, `ping-ring`, `threat-heat`, `wizard-slide`, `reactor-spin`, `shockwave-active`.
   - Большой заголовок-комментарий в `src/index.css` полностью удалён.
4. **Tauri v2 монолит**:
   - Подготовлена подпапка `tauri-v2/` с прямым мостом `tauriBridge.ts`.
