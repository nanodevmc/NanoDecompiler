# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.3 (dev)
- **Стек**: C++20 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Tauri v2 (`tauri-v2/`), Electron + React/Vite фронтенд, Avalonia .NET 8 (`NanoDecompilerLite`).
- **Окружение**: Windows 10, слабый процессор, 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать последовательно.
  - `test_jars` неприкосновенен для GitHub Actions.

## Прогресс перехода на Tauri v2 & Mobile:
1. **Монолитное ядро (`tauri-v2/src-tauri`)**:
   - `MonolithEngine`: объединение `nanodecompiler-fastscan` и декомпилятора в единый нативный процесс Rust без внешних CLI.
   - Асинхронный запуск задач и потоковый стриминг событий в GUI (`decompile:progress`, `decompile:log`, `decompile:finish`).
   - Кроссплатформенные пути (`get_app_data_dir()`, автоопределение `NanoDecompilerCLI` под Windows/Linux/macOS/Android, кроссплатформенные редакторы в `editors.rs`).
   - Релизный бинарник `nanodecompiler-tauri.exe` (8.84 МБ) собран с иконкой и выложен на Рабочий стол (`NanoDecompiler-Tauri-v2.exe`).
2. **Мобильная версия (Android 7.0 - 16+)**:
   - Полностью переписана на чистый Kotlin без кривых мостов: адаптивный Material Design 3 + Liquid Glass UI.
   - Скомпилированы под Android ARM64 (`aarch64-linux-android`) официальным Google NDK r26c LLVM toolchain:
     * C++20 декомпилятор: `libnanodecompiler_cli.so` (2.48 МБ) со статическим `-static-libstdc++` (символы `std::filesystem` полностью включены).
     * Дополнительно вложена `libc++_shared.so` (1.82 МБ).
     * Rust FastScan сканер энтропии и малвари: `libnanodecompiler_fastscan.so` (1.0 МБ).
   - CFR полностью удалён из проекта и тулчейна.
   - Google Material Icons (папка, плей, шестеренка, терминал, код, очередь, корзина).
   - Liquid Glass: фоновые неоновые световые сферы (`#9BF246`, `#29B6F6`), полупрозрачные стеклянные карточки с бликом, анимация дыхания дропзоны.
   - Окно Настроек (деобфускация AegisVM, проверка легитимности, поиск уязвимостей, контакты).
   - Терминал стримит реальный вывод C++ ядра (баннер `❖ NanoDecompiler v2.0.3`, PASS 1..3) без искусственных префиксов.
   - Строгая фильтрация файлов: поддерживаются `.jar`, `.class`, `.zip`, `.tar.gz`, `.7z`, `.rar`.
   - Автор / GitHub: `nanodev1488`.
   - Создан постоянный Release Keystore (`C:\Users\nanodev\Desktop\ZL1_Secrets\nanodecompiler-release.keystore`), пароль `regi12345.`, алиас `nanodev`, сертификат действует до 2054 года.
   - Данные для магазинов и Base64 для GitHub Actions Secrets сохранены в `C:\Users\nanodev\Desktop\ZL1_Secrets\RELEASE_KEYSTORE_INFO.txt`.
   - GitHub Actions workflow (`.github/workflows/build-and-release.yml`) обновлен для автоматической релизной подписи APK через секреты `ANDROID_KEYSTORE_BASE64` и `ANDROID_KEYSTORE_PASSWORD`.
   - Устранена медленная декомпиляция (дебаг-спам `[PASS: ...]` и `[DBG: ...]` вычищен из `structure.cpp` и `engine.cpp` C++ ядра, возвращена скорость нативного C++).
   - Внедрена кнопка «Остановить» декомпиляцию (красный стиль `#E53935`, белый текст, иконка ⏹, `Process.destroyForcibly()`).
   - 5 полноценных вкладок: Главная, Архивы, Редактор кода (дерево, фильтр, подсветка синтаксиса Java, кнопка копирования, Wrap), Терминал, Настройки (деобфускация AegisVM, проверка легитимности, поиск уязвимостей, виброотклик, автор nanodev1488).
   - Собран и официально подписан Release APK: `NanoDecompiler-Mobile-v2.0.3.apk` (3.92 МБ, ключ `nanodev` из `ZL1_Secrets`).
   - Загружен на `https://paste.c-net.org/FistsValerie` и скопирован на Рабочий стол (`C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.3.apk`).
3. **UI, Liquid Glass и MD3**:
   - Реализован Liquid Glass (`liquid-glass`, `md3-card`, `md3-pill`, `radar-vivid`, `scanline-glow-bar`, `orb-vivid`).
   - Адаптивный layout для Mobile (`md:min-w-[960px]`, адаптивный Titlebar).
   - `CodeView.tsx`: футер переработан в компактный непереносимый чип (`L · c · RO`), устранено сползание слоями.
4. **GitHub Actions и Релизы**:
   - Оптимизирован ccache движка на базе hashFiles, добавлен `Swatinem/rust-cache@v2`.
   - Внедрена джоба `build-tauri-android` для сборки `NanoDecompiler-Android-arm64.apk` (Android 7.0+).
   - В релизы включены: Tauri v2 (Windows, Linux, Android), Electron (Windows, Linux, macOS), Avalonia Lite, CLI.
   - `CHANGELOG.md` полностью синхронизирован и структурирован для v2.0.2 и v2.0.3.
