# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.7 (Standardized Release & Lightweight Ecosystem)
- **Стек**: C++17 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Tauri v2 Native (`tauri-v2`), Electron/React (TypeScript, Tailwind, Liquid Glass), Android Kotlin (Material Design 3), C# Avalonia Lite (`src_lite_cs`).
- **Окружение**: Windows 10, процессор Intel Celeron P4600 (2 ядра, 2 потока), 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать строго последовательно (1 процесс за раз).
  - СТРОГО БЕЗ `python -c` (любой вызов Python только через прямые файлы `.py`).
  - СТРОГО БЕЗ хардкода токенов и секретов в файлах репозитория.

## Стандартизация релизов и сквозное подключение OTA (v2.0.7):
1. **Канонический порядок именования релизных артефактов**:
   - Каждый файл строго декларирует технологию платформы: `[Name]-[Platform]-[OS]-[Arch]-[Version][Ext]`.
   - `NanoDecompiler-Tauri-*` (основной нативный клиент: Windows Setup, Portable, Linux).
   - `NanoDecompiler-Electron-*` (классический клиент fallback: Windows, Linux AppImage, macOS DMG).
   - `NanoDecompiler-Lite-*` (ультралегкий C# GUI: Windows, Linux, macOS x64, macOS arm64).
   - `NanoDecompiler-CLI-*` (C++ ядро декомпилятора и headless API сервер).
   - `NanoDecompiler-Android-*` (нативное мобильное приложение).
   - `NanoDecompiler-OTA-*` (легковесные дельта-архивы для всех GUI компонентов).
2. **Ликвидация тяжеловесного моста Android и оптимизация APK**:
   - Устранен 12.8 МБ мост Tauri mobile; сборка переведена на нативный Kotlin (`scripts/build_android.py`).
   - Итоговый размер релизного подписанного APK уменьшен до **2.87-2.97 МБ** (-76.8% редукция).
   - Удален случайный 2.58 МБ бинарник из `mobile-android/assets/bin/`, очищены устаревшие чанки Vite.
3. **Легковесные дельта-пакеты OTA (.ndota)**:
   - Полное покрытие всех 4 GUI платформ без включения бинарников движка и .map-файлов:
     - `NanoDecompiler-OTA-WebUI-v2.0.7.ndota`: ~199 KB (Web/SPA фронтенд)
     - `NanoDecompiler-OTA-Electron-v2.0.7.ndota`: ~289 KB (Renderer + Preload)
     - `NanoDecompiler-OTA-Android-v2.0.7.ndota`: ~199 KB (WebView ассеты)
     - `NanoDecompiler-OTA-Lite-v2.0.7.ndota`: ~31 KB (Avalonia XAML темы и иконки)
4. **Сквозная интеграция OTA во всех платформах**:
   - **Android**: 100% нативно в `MainActivity.kt` (опрос `ota-manifest.json`, фоновое скачивание, SHA-256 верификация, PackageInstaller).
   - **Electron**: `OtaManager` с авто-следованием за HTTP 301/302 редиректами GitHub CDN, поддержка секции `otaPackages`.
   - **Tauri**: `tauriBridge.ts` без моков — живой `fetch()` к `ota-manifest.json`, семантическое сравнение версий и потоковая загрузка `.ndota`.
   - **UI**: `UpdateModal.tsx` динамически отображает и скачивает реальные `.ndota` пакеты.

## 5 Мега-Галактических Инноваций движка:
1. **NanoNeural Semantic Source Reconstructor (`semantic_reconstructor.cpp`, `semantic_reconstructor.hpp`)**:
   - Контекстно-энтропийный анализ потока вызовов, семантическое восстановление параметров (`Player player`, `Location targetLocation`, `CommandSender sender`).
2. **Holographic Reactive Dependency Graph & Exploit Vector Matrix (`dependency_graph.cpp`, `HolographicGraphModal.tsx`)**:
   - Экспорт топологии плагина в JSON (`--export-graph`) с 6 архитектурными слоями и тепловой картой эксплойтов.
3. **Adaptive Hardware-Aware Swarm & Bytecode Deduplication Engine (`decomp_cache.cpp`, `decomp_cache.hpp`)**:
   - Локальный swarm без сторонних установок, персистентный SHA-256 кеш байткода.
4. **Universal Paper/Folia Live Transpiler & Auto-Shim (`rejar_folia.cpp`, `rejar_folia.hpp`)**:
   - Портирование Bukkit-плагинов под многопоточную Folia 1.21.4 в один шаг: `--port-folia`.
5. **Deep Memory Zero-Day Vulnerability Hunter & Bytecode Fuzzing Harness (`fuzzing_harness.cpp`, `fuzzing_harness.hpp`)**:
   - Символьный фаззер уязвимостей дюпов инвентаря и авто-вакцинация байткода (`--immunize`).
