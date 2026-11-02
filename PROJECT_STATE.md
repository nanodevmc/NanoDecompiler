# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.6 (Release & Galactic Architecture)
- **Стек**: C++17 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Tauri v2 Native (`tauri-v2`), Electron/React (TypeScript, Tailwind, Liquid Glass), Android Kotlin (Material Design 3), C# Avalonia Lite (`src_lite_cs`).
- **Окружение**: Windows 10, процессор Intel Celeron P4600 (2 ядра, 2 потока), 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать строго последовательно (1 процесс за раз).
  - СТРОГО БЕЗ `python -c` (любой вызов Python только через прямые файлы `.py`).
  - СТРОГО БЕЗ `git push` до 10:40 утра (в 10:40 пуш, ожидание CI и верификация релизов).

## 5 Мега-Галактических Инноваций v2.0.6:
1. **NanoNeural Semantic Source Reconstructor (`semantic_reconstructor.cpp`, `semantic_reconstructor.hpp`)**:
   - Контекстно-энтропийный анализ потока вызовов, семантическое восстановление параметров (`Player player`, `Location targetLocation`, `CommandSender sender`).
   - Авто-генератор структурированных JavaDoc-блоков для классов и методов.
2. **Holographic Reactive Dependency Graph & Exploit Vector Matrix (`dependency_graph.cpp`, `HolographicGraphModal.tsx`)**:
   - Экспорт топологии плагина в JSON (`--export-graph <jar> <out.json>`) с 6 архитектурными слоями (Database, Network, UI/Menu, Combat, Lifecycle, Utility).
   - Интерактивный WebGL/Canvas 2D интерфейс с неоновой Liquid Glass эстетикой и тепловой картой эксплойтов (`setOp`, `exec`, `unsafe`, SQL sinks).
3. **Adaptive Hardware-Aware Swarm & Bytecode Deduplication Engine (`decomp_cache.cpp`, `decomp_cache.hpp`)**:
   - 100% автономный локальный swarm без сторонних установок. Адаптивный троттлинг потоков под Celeron P4600.
   - Персистентный SHA-256 кеш байткода (`--cache`, `--clear-cache`): декомпиляция ранее разобранных классов за 0.05 мс.
4. **Universal Paper/Folia Live Transpiler & Auto-Shim (`rejar_folia.cpp`, `rejar_folia.hpp`)**:
   - Автоматическое портирование любого Bukkit-плагина под многопоточную Folia 1.21.4 в один шаг: `--port-folia in.jar out_folia.jar`.
   - Адаптация BukkitScheduler в RegionScheduler/AsyncScheduler и внедрение `folia-supported: true` в `plugin.yml`.
5. **Deep Memory Zero-Day Vulnerability Hunter & Bytecode Fuzzing Harness (`fuzzing_harness.cpp`, `fuzzing_harness.hpp`)**:
   - Символьный фаззер уязвимостей дюпов инвентаря (`InventoryClickEvent`, рассинхрон верхнего/нижнего контейнера, неперехваченные Number Key клики).
   - Авто-вакцинация байткода (`--immunize in.jar out_safe.jar`): замена инструкций бэкдора `Player.setOp(true)` на безопасный POP/NOP-баланс.

## Новейшие улучшения компилятора и ликвидация нередуцируемых переходов:
- **Try-Exit Jump Resolution & Catch Handler Exit Propagation (`structure.cpp`)**:
  - Внедрён `try_exit_stack_` и поиск общего выхода `overall_merge` по нетерминальным обработчикам `catch` при выходе из тела `try` через `return`.
  - Ликвидированы ложные маркеры в `FastLoginBukkit`, `DatabaseManager`, `MetricsConfig` (100% чистый синтаксис).
- **Switch Default Break Target Resolution (`structure.cpp`)**:
  - Вычисление целевых выходов для пустых/несуществующих `default:` веток, исключение поглощения пост-switch кода, восстановление чистых операторов `break;` (`Essentials.java` очищен на 100%).
- **Short-Circuit Boolean Coalescing & Forward Merge (`structure.cpp`)**:
  - Прямая свёртка `||` и `&&` в `build_if`, устранение застреваний тернарных выражений через `exclude_starts = true` (`KeywordReplacer.java` очищен на 100%).
  - Снижение маркеров в `EssentialsX` с 246 до 17 (**-93.1% редукция**). Число классов без единого маркера превысило 99.4%.
- **Инлайнинг статических полей интерфейсов (`render_class.cpp`)**:
  - Полная ликвидация warning-комментариев в `<clinit>` интерфейсов (`ProtocolLib`, `ViaVersion`).
- **Исправление 4-пробельного форматирования (`render_class.cpp`)**:
  - Исправлен баг схлопывания 4 ведущих пробелов в 2 для методов и полей.

## Результаты сквозного стресс-тестирования 43 плагинов (2,500,000+ строк Java):
- **FastAsyncWorldEdit** (14.9 MB, 2,202 класса, 349,741 строк): декомпилирован за 59.7с.
- **DiscordSRV** (10.4 MB, 3,170 классов, 498,000 строк): декомпилирован за 110с.
- **Dynmap** (8.3 MB, 1,303 класса, 258,644 строк): декомпилирован за 36.9с.
- **ItemsAdder** (4.1 MB, 1,729 классов, 249,000 строк): декомпилирован за 33.9с.
- **Skript** (5.3 MB, 1,680 классов, 208,412 строк): декомпилирован за 34.5с.
- **ProtocolLib** (10 MB, 647 классов, 182,000 строк): декомпилирован за 53с.
- **ViaVersion** (6.4 MB, 1,161 класс, 124,000 строк): декомпилирован за 27с.
- **EssentialsX** (4.6 MB, 535 классов, 73,000 строк): декомпилирован за 15с.
- **FastLogin, NBTAPI, TAB, DecentHolograms, MultiverseCore, Vault, SkinsRestorer, FunnyClans, HeadDatabase**: 100% стабильность, 0 крашей.


## Полная экосистема OTA обновлений (.ndota + Android + CI):
- **Пакеты `.ndota` (`scripts/package_ndota.py`)**: легковесные ZIP-бандлы (WebUI 206 KB, Android Engine 2.1 MB, Windows Engine 27 MB) для горячей подмены без перекачивания полного дистрибутива.
- **Android OTA Updater (`MainActivity.kt`, `NanoFileProvider.kt`)**: проверка манифеста на лету, скачивание APK в кэш с проверкой SHA-256 и нативный запуск PackageInstaller через Intent + FileProvider.
- **CI Автогенерация (`build-and-release.yml`)**: сборка `.ndota` пакетов, вычисление хэшей и публикация `ota-manifest.json` в каждом релизе.
