# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.3 (dev)
- **Стек**: C++20 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Tauri v2 (`tauri-v2/`), Electron + React/Vite фронтенд, Avalonia .NET 8 (`NanoDecompilerLite`).
- **Окружение**: Windows 10, слабый процессор, 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать последовательно.
  - `test_jars` неприкосновенен для GitHub Actions.

## Прогресс перехода на Tauri v2 (~85%):
1. **Монолитное ядро (`tauri-v2/src-tauri`)**:
   - `MonolithEngine`: объединение `nanodecompiler-fastscan` и декомпилятора в единый нативный процесс Rust.
   - Асинхронный запуск задач и потоковый стриминг событий в GUI (`decompile:progress`, `decompile:log`, `decompile:finish`).
   - Модули `commands.rs`, `editors.rs`, `network.rs`, `windows.rs`, `engine_monolith.rs`.
2. **Бесшовный мост (`src/lib/tauriBridge.ts`)**:
   - Эмуляция `window.nano` через `__TAURI_INTERNALS__`:
     `selectJar`, `selectOutDir`, `jarSummary`, `scanJar`, `runDecompile`, `checkEnv`, `openPath`, `openExternal`, оконные команды.
   - Фронтенд работает без изменений кода как в Tauri v2, так и в Electron.
3. **UI, Liquid Glass и MD3**:
   - Реализован Liquid Glass (`liquid-glass`, `md3-card`, `md3-pill`, `radar-vivid`, `scanline-glow-bar`, `orb-vivid`).
   - `CodeView.tsx`: футер переработан в компактный непереносимый чип (`L · c · RO`), устранено сползание слоями.
4. **GitHub Actions и кэш C++ движка (исправлено)**:
   - Причина сброса кэша устранена: ключи переведены с `github.run_id`/`github.sha` на `${{ hashFiles('resources/engine_cpp/**') }}`.
   - В Windows MSYS2 задан явный `CCACHE_DIR` в workspace (`.ccache`), добавлен вывод статистики `ccache -s`.
   - Внедрен кэш Rust Cargo (`Swatinem/rust-cache@v2`).
   - Добавлены джобы `build-tauri-windows` и `build-tauri-linux`: Tauri v2 стал основным релизным клиентом в Actions, Electron сохранен как fallback.
   - Добавлен `concurrency: cancel-in-progress: true`.
