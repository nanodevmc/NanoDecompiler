# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.2 (rev3)
- **Стек**: C++20 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Tauri v2 подпапка (`tauri-v2/`), Electron + React/Vite фронтенд, Avalonia .NET 8 (`NanoDecompilerLite`).
- **Окружение**: Windows 10, слабый процессор, 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать последовательно, беречь память.

## Что сделано в rev3:
1. **Начальное меню / SetupWizard (по скринам)**:
   - Адаптивная ширина карточки под контент с запасом: `max-w-[520px]`, лицензия `max-w-[720px]`, настройки `max-w-[620px]` (с плавной анимацией ресайза `transition-all duration-300`).
   - Кибер-фон с неоновой координатной сеткой (`cyber-grid-pattern`) и двумя плавающими светящимися орбитами (`animate-cyber-glow` кислотная и `animate-cyber-glow-alt` лазурная).
   - Интерактивный прогресс-бар этапов в шапке карточки.
   - Анимационные сдвиги контента влево при переходе (`animate-wizard-slide-out` и `in`).
2. **4 ключевые фичи в меню «Инструменты v2.0»**:
   - `SecurityAuditModal`: сканер бэкдоров (ForceOP `setOp(true)`, RCE `ProcessBuilder`, Discord-вебхуки, кража `server.properties`, скрытые сокеты).
   - `PaperExportModal`: экспорт в Maven/Gradle под Paper 1.21.4 + детектор несовместимостей с Folia (`RegionScheduler`).
   - `EventFlowModal`: интерактивный граф и таблица слушателей Bukkit `@EventHandler` с приоритетами и фильтрацией.
   - `DeobfMappingModal`: справочник соответствий Mojang / Spigot / NMS.
3. **Подпапка `tauri-v2/`**:
   - Создана структура `tauri-v2/src-tauri` с `tauri.conf.json`, `Cargo.toml`, командами Rust и привязкой к общему `dist/` фронтенду (1-в-1 стили, цвета и темы).
