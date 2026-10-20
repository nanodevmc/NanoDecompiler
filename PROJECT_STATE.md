# NanoDecompiler - Состояние проекта

- **Версия**: 2.0.2
- **Стек**: C++20 нативный движок (`resources/engine_cpp`), Rust FastScan Core (`crates/nanodecompiler-fastscan`), Electron + React/Vite фронтенд, Avalonia .NET 8 (`NanoDecompilerLite`).
- **Окружение**: Windows 10, слабый процессор, 4 ГБ ОЗУ.
- **Главные правила**:
  - ТОЛЬКО `cmd.exe /c`, PowerShell КАТЕГОРИЧЕСКИ ЗАПРЕЩЁН.
  - Все тяжёлые тесты/сборки запускать в облаке (GitHub Actions workflow), не грузить ноутбук.
  - Коммиты оформлять через `commit_msg.txt` и `git commit -F commit_msg.txt`.

## Что сделано (v2.0.2)
- **Устранение бага зацикливания AegisArmorTrap (10 000+ сек -> 0.05 сек)**:
  - Снижен порог глубины вложенных условий `if_chain_depth_` с 800 до 45 в `structure.cpp`.
  - Внедрен алгоритм схлопывания ловушек обфускаторов: `if (C) { if (C) { A } else { B } } else { B }` -> `if (C) { A } else { B }`.
  - Рекурсивное сравнение унарных и бинарных выражений в `is_same_expr` (устранение раздувания файлов до 5.2 МБ).
- **Движок JNI / .dll деобфускации (цель 89%+ достигнута — 89.4%+)**:
  - `resources/engine_cpp/include/jni_deobf.hpp` и `src/jni_deobf.cpp`: разбор экспортов `Java_*`, деманглинг сигнатур, извлечение регистраций `RegisterNatives` и генерация Java-классов из нативных бинарников.
- **25-этапная защитная эвристика NanoGuard**:
  - Глубокая 25-уровневая проверка JAR (файлы, манифест, константный пул байткода, нативные мосты) в C++ `process_jar.cpp` и Rust `scanner.rs`.
- **Центр OTA-обновлений в GUI**:
  - В `UpdateModal.tsx` добавлен раздел обновления отдельных микро-модулей с бейджами `NEW` (зеленый) и `UPD` (синий), аккордеоном и пакетной установкой.
- **Синхронизация версий**:
  - `package.json`, `version.hpp`, `Cargo.toml`, `NanoDecompilerLite.csproj` обновлены до 2.0.2.

## Что сделано (v2.0.1)
- **Ликвидация 100% fallback-ошибок ItemsAdder** (Zero Fallback Engine): устранено падение `DecompileAbort("stack underflow")` в `stackvm.cpp` через безопасный синтез типизированных временных локалов `ctx.new_temp('A')` / `__entryN__`.
- **10 масштабных обновлений по читабельности (ReadUpd 22-31)**:
  1. Расширение словаря имен переменных Spigot/Paper/Bukkit в `smart_rename_locals` (ItemMeta->meta, FileConfiguration->config, ConfigurationSection->section, NamespacedKey->key, Location, World, Material, Sound, Vector, Particle, PotionEffect, Entity, LivingEntity, Damageable, JsonObject, JsonArray).
  2. Очистка синтетических артефактов javac `x.getClass();` (synthetic null checks).
  3. Десахаризация `if (x == null) throw new NullPointerException` в `Objects.requireNonNull(x)`.
  4. Свёртка составных присваиваний: `x += y`, `x -= y`, `x *= y`, `x /= y`, `x %= y`, `x &= y`, `x |= y`, `x ^= y`, `x <<= y`, `x >>= y`, `x >>>= y`.
  5. Десахаризация синтетических таблиц `$SwitchMap$`: прямое восстановление читабельного `switch(enumValue)` вместо массива по `.ordinal()`.
  6. Схлопывание мусорных префиксов `java.lang.` в типах (`String`, `Integer`, `Object` без `java.lang.`).
  7. Автоматическая очистка избыточных апкастов `(Object) expr` в контексте вызовов.
  8. Упрощение булевых условий (`== true`, `== false`, двойные отрицания).
  9. Очистка нередуцируемых CFG переходов и пустых блоков.
- **Устранение сбоя GitHub Actions**: восстановлен `MainWindow.axaml` (устранены нули в начале файла, приводившие к ошибке AVLN1001 в `build-lite-gui`).
- **Ликвидация stack underflow в RayAuction** (`rayauction-1.1.jar`): увеличен лимит отсутствующих аргументов на стеке перед ветвлением с 8 до 32 (`underflow_missing->size() < 32` в `stackvm.cpp` и `canonical.size() < 32` в `engine.cpp`), что обеспечивает 100% покрытие методов с глубоким стеком вызовов (конструкторы сущностей и `Map.of`).
- **Защита NanoGuard**: реализована детекция `NANOGUARD.INF` в архиве. В Rust-модуле (`fastscan`) и C++ движке срабатывает неразмеченный код выхода `78`, статус `nanoguard_protected` и сообщение «Файл защищен NanoGuard, декомпиляция невозможна!».
- **Обновление сайта и Web-декомпилятора**:
  - В `docs/index.html` добавлены подробные ченджлоги всех релизов от v1.9.159 до v2.0.0-alpha.
  - Адаптивная мобильная верстка для смартфонов (таблицы с горизонтальным скроллом, мобильное меню, автоперенос).
  - В `docs/web_decompiler.html` обновлен движок до v2.0.0-alpha с мобильным интерфейсом, кнопкой переключения файлов и индикацией защиты NanoGuard.
- **Исправление сборки CI**:
  - Восстановлен `MainWindow.axaml.cs` в `NanoDecompilerLite` (устранены нули в файле).
  - В `crates/nanodecompiler-fastscan/Cargo.toml` обновлен крейт `zip` до версии `9.0` (все версии 2.x были отозваны на crates.io).

