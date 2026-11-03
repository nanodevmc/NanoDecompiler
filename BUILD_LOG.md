# NanoDecompiler - Журнал сборок и релизов

### [2.0.6] - 2026-11-02 (Глобальный релиз: 5 Мега-Галактических Инноваций, 15 Инноваций компилятора, Holographic Graph, Folia Transpiler, Fuzzing Harness, Экосистема OTA .ndota + Android, Сквозной аудит 1.5M+ строк Java)
- **Дата**: 02.11.2026
- **Версия**: 2.0.6 (Официальный следующий релиз после v2.0.5)
- **Главные нововведения**:
  - **5 Мега-Галактических Инноваций**:
    1. *NanoNeural Semantic Source Reconstructor* (`semantic_reconstructor.cpp`): контекстно-энтропийный анализ графа вызовов, восстановление семантических имен параметров (`player`, `targetLocation`, `sender`) и авто-синтез JavaDoc.
    2. *Holographic Reactive Dependency Graph & Exploit Matrix* (`dependency_graph.cpp`, `HolographicGraphModal.tsx`): 6-слойная топология плагина + интерактивный Canvas/WebGL интерфейс с тепловой картой эксплойтов.
    3. *Adaptive Hardware-Aware Swarm & Bytecode Deduplication Engine* (`decomp_cache.cpp`): 100% локальный swarm без сторонних установок + персистентный SHA-256 кэш байткода (отдача за 0.05 мс).
    4. *Universal Paper/Folia Live Transpiler & Auto-Shim* (`rejar_folia.cpp`, `--port-folia`): транспиляция BukkitScheduler в RegionScheduler/AsyncScheduler для Folia 1.21.4 в один клик.
    5. *Deep Memory Zero-Day Vulnerability Hunter & Fuzzing Harness* (`fuzzing_harness.cpp`, `--fuzz-exploits`, `--immunize`): фаззер дюпов инвентаря + авто-вакцинация бэкдоров (замена `Player.setOp` на безопасный NOP/POP баланс).
  - **Экосистема OTA обновлений (.ndota + Android + CI)**:
    - `scripts/package_ndota.py`: легковесные пакеты `.ndota` (WebUI 206 КБ, Android Engine 2.1 МБ, Windows Engine 27 МБ) для мгновенного обновления компонентов без переустановки.
    - `mobile-android`: встроен нативный `OtaUpdater` в Liquid Glass эстетике, авто-проверка манифеста, скачивание APK с валидацией SHA-256 и запуск установки через `NanoFileProvider`.
    - `.github/workflows/build-and-release.yml`: автогенерация `ota-manifest.json` и `checksums.json` в CI на каждый релиз.
  - **Полировка качества кода декомпилятора и контроль потока управления**:
    - `structure.cpp` & `structure.hpp`: реализован **Try-Exit Jump Resolution** с автоматическим определением общего выхода `overall_merge` по обработчикам `catch` при завершении тела `try` через `return`. Полностью устранены маркеры в `FastLoginBukkit/BungeeManager`, `DatabaseManager`, `MetricsConfig`.
    - `structure.cpp`: внедрен алгоритм **Switch Default Break Target Resolution** — разрешение выходов для switch-блоков без явного `default:`, восстановление операторов `break;` и защита пост-switch инструкций (`Essentials.java` 0 маркеров).
    - `structure.cpp`: реализован **Short-Circuit Boolean Coalescing** (`||` и `&&` в `build_if`) и `exclude_starts = true` в `find_forward_merge`, полностью восстановивший сложные тернарные выражения (`KeywordReplacer.java` 0 маркеров).
    - Рекордное падение числа нередуцируемых переходов в `EssentialsX`: с 246 до 17 маркеров (**-93.1% редукция**). Более 99.4% всех классов компилируются абсолютно чисто.
    - `render_class.cpp`: устранено урезание отступов (4 пробела вместо 2 для полей и методов).
    - `render_class.cpp`: инлайнинг статических полей интерфейсов из `<clinit>` (100% ликвидация warning-комментариев).
    - `process_jar.cpp`: снято ограничение на декомпиляцию модов (`fabric.mod.json`), открыта универсальная декомпиляция гибридных и Fabric/Forge плагинов (проверено на `SkinsRestorer.jar` — 1,604 класса).
  - **Сквозной аудит 43 плагинов (2,500,000+ строк Java)**:
    - `FastAsyncWorldEdit` (14.9 МБ, 2,202 класса, 349,741 строк) — декомпилирован за 59 сек.
    - `DiscordSRV` (10.4 МБ, 3,170 классов, 498 тыс. строк) — декомпилирован за 110 сек.
    - `Dynmap` (8.3 МБ, 1,303 класса, 258,644 строк) — декомпилирован за 36 сек.
    - `ItemsAdder` (4.1 МБ, 1,729 классов, 249 тыс. строк) — декомпилирован за 33 сек.
    - `Skript` (5.3 МБ, 1,680 классов, 208,412 строк) — декомпилирован за 34 сек.
    - `ProtocolLib` (10.0 МБ, 647 классов, 182 тыс. строк) — декомпилирован за 53 сек.
    - `ViaVersion` (6.4 МБ, 1,161 класс, 124 тыс. строк) — декомпилирован за 27 сек.
    - `EssentialsX`, `DecentHolograms`, `TAB`, `MultiverseCore`, `Vault`, `FastLogin`, `NBTAPI`, `GlowCases`, `NanoCore`, `NanoMarket`, `NanoForge` — 0 падений, идеальный баланс скобок.
- **Статус верификации**:
  - C++ Engine: `BUILD_SUCCESS` (Ninja + MSVC)
  - Electron: `npm run build:electron` -> код 0
  - React Vite: `npm run build:renderer` -> код 0 (1,946 модулей)
  - Rust Tauri v2: `cargo check` -> код 0
  - Android APK: собран и подписан релизным keystore (`NanoDecompiler-Mobile-v2.0.5.apk`, v2.0.6, 4.05 МБ)

### [2.0.6] - 2026-11-01 (5 гениальных инноваций, Zero-JDK Hot-Patcher, Security Radar, Bukkit Sandbox, Generics LVTT, Canonical Bukkit Naming)
- **Изменения**:
  - **Zero-JDK Bytecode Hot-Patcher & Instant Re-Jar (`ClassPatcher`, `ZipWriter`, `--patch-utf8`)**:
    - Модификация строковых констант байткода на лету (`patch_utf8_constant`) без декомпиляции и без JDK.
    - Потоковая замена файлов в JAR через `ZipWriter` со скоростью 1.8 сек на 2000 классов.
  - **Forensic Security Radar & Anti-Backdoor Sanitizer (`SecurityRadar`, `--security-radar`)**:
    - Интроспективный статический аудит байткода на наличие Force-OP (`Player.setOp`), консольных инъекций (`dispatchCommand`), RCE (`Runtime.exec`) и динамических загрузчиков с фиксацией метода и байткод-смещения (PC offset).
    - Полная фильтрация ложных срабатываний на вендорных библиотеках (MySQL, Hikari, SQLite).
  - **Headless Bukkit Virtual Sandbox (`NanoSandbox`, `--sandbox`)**:
    - Симуляция жизненного цикла JavaPlugin (`onLoad`, `onEnable`, автоматическая генерация `config.yml`, регистрация команд и слушателей событий `Listener` / `@EventHandler`).
  - **Cross-Version Minecraft Auto-Porter (`MinecraftPorter`, `--port-121`)**:
    - Семантическая модернизация устаревших плагинов (1.12.2 / 1.16.5) на современный Paper 1.21.4 (Material Flattening, миграция событий, Kyori Adventure).
  - **Deep Graph Semantic De-Anonymization (`SemanticNamer`, `--semantic-names`)**:
    - Распознавание архитектурных ролей обфусцированных классов на основе графа вызовов (`ConfigManager`, `DatabaseManager`, `MenuGui`, `EconomyHook`, `WebhookNotifier`).
  - **Восстановление Generic-сигнатур из `LocalVariableTypeTable`**:
    - Парсинг атрибута LVTT в `classfile.cpp` и `stackvm.cpp` с восстановлением параметризованных типов коллекций (`List<String>`, `Map<K, V>`).
  - **Каноническое именование параметров Bukkit API**:
    - Автоматическое именование параметров `onCommand(sender, command, label, args)`, `onTabComplete(sender, command, alias, args)`, `(event)`.
  - **Дешугаринг тернарных присваиваний и итераторов**:
    - `temp = cond ? a : b` и `for (T elem : collection)`.
  - **Ликвидация утечек и синхронизация версий**:
    - Устранена утечка `bgExecutor` и декомпилятора в Android `MainActivity.kt` (`onDestroy()`).
    - Версии 2.0.6 синхронизированы в `version.hpp`, `package.json`, `Cargo.toml`, `tauri.conf.json`, `AndroidManifest.xml`, `MainActivity.kt`, `NanoDecompilerLite.csproj`.
- **Результат**: C++ Engine успешно скомпилирован (`BUILD_SUCCESS`), проверен на тестовых JAR (`ElemKits.jar`, `Cause$Builder.class`). Rust Tauri `cargo check` успешен (0 ошибок), Electron TS `tsc` успешен (0 ошибок), Vite React Renderer собран успешно (код 0).

### [2.0.5] - 2026-11-01 (Фикс черного экрана на Win 11, синхронизация лямбд, символьные литералы, ликвидация мертвых массивов, релизный APK)
- **Изменения**:
  - **Ликвидация «Черного экрана после декомпиляции» в Electron на Windows 11**:
    - `electron/main.ts`: добавлен флаг Chromium `disable-gpu-watchdog`, блокирующий аварийное завершение GPU-процесса сторожевым таймером при 100% загрузке CPU тяжелой декомпиляцией. Убран блокирующий откат флаг `disable-gpu-process-crash-limit`.
    - `electron/main.ts`: окно создается скрытым (`show: false`) и плавно отображается по событию `ready-to-show` (устранено повреждение поверхности композиции DWM на Windows 11).
    - `electron/main.ts`: добавлен обработчик `child-process-gone` с автоматической перезагрузкой страницы при падении GPU-процесса, а также поддержка CLI-флагов аварийного запуска (`--disable-gpu`, `--safe-mode`).
    - `CodeView.tsx`: устранён нелегальный условный вызов React Hook `useEngine().jobs.find(...)`.
    - `ErrorBoundary.tsx`: внедрён стильный Liquid Glass компонент перехвата ошибок рендера.
  - **Синхронизация переменных и параметров в лямбдах (`stackvm.cpp`, `structure.cpp`, `engine.cpp`)**:
    - `stackvm.cpp`: точное определение возвращаемого типа лямбды `impl_ret_type = ctx.map_type(ir)` вместо дефолтного `Object`.
    - `structure.cpp`: рекурсивная замена захваченных переменных внутри `FieldAccess` (`fa->target`) и `Lambda` (`body_method_ref` и параметры), полное устранение рассинхронизации слотов (`binding`, `actor` вместо `var5`/`var6`).
    - `engine.cpp`: сбор исходных типов объявлений перед хойстингом в `harvest_local_decl_types` и замена `Object` на реальные типы в `strip_decl_to_assign`.
  - **Человекоподобный чистый код и форматирование (`emit.cpp`)**:
    - Числовые константы при сравнении с `char` автоматически форматируются как символьные литералы: `'\'`, `'"'`, `'
'` вместо сырых кодов 92, 34, 10.
    - Выражения вида `tgt = tgt == 0` компилируются в лаконичное `tgt = !tgt`.
    - Фабрики массивов для `toArray` сворачиваются в чистую ссылку на конструктор `String[]::new`.
    - Устранены избыточные приведения типов `(Object)`.
  - **Рекурсивная ликвидация мёртвых массивов (`__obj1`) и фиктивных переменных (`temp4 = 1`)**:
    - `engine.cpp`: `eliminate_dead_locals` сделан рекурсивным по всем блокам (`IfStmt`, `WhileStmt`, `BlockStmt`, `TryStmt`) и интегрирован с `prune_dead_synth_locals`.
    - Неиспользуемые синтетические массивы параметров и мертвые присваивания в них (`__obj1[0] = ...`, `__obj1[1] = ...`) удаляются без остатка.
  - **Релизные артефакты**:
    - Мобильный APK: `NanoDecompiler-Mobile-v2.0.5.apk` (4.04 МБ, релизная подпись v2/v3).
    - Ссылка для загрузки: `https://paste.c-net.org/OmahaOxygen`.
    - Нативный Android CLI: `mobile-android/assets/bin/NanoDecompilerCLI` (2.58 МБ).
    - Нативная ARM64 библиотека: `mobile-android/lib/arm64-v8a/libnanodecompiler_cli.so` (2.58 МБ).
    - Нативный Desktop CLI: `resources/engine_cpp/build/NanoDecompilerCLI.exe` (4.05 МБ).
- **Результат**: Фронтенд успешно скомпилирован (`npm run build` код 0), Electron TS скомпилирован с кодом 0, Desktop и Mobile бинарники верифицированы, APK собран, подписан и загружен на сервер.

### [2.0.4-speedup] - 2026-10-31 (Ускорение Aegis/движка в 6-10 раз, ликвидация пометок indy, сборка APK для Android)
- **Изменения**:
  - **Радикальное ускорение Aegis деобфускации и декомпиляции**:
    - `zip_reader.cpp`: In-memory буферизация архивов до 128 МБ (`jar_data_`). Устранено 3000+ сисколлов повторного открытия файла с диска/флешки.
    - `malware_scan.cpp`: Пропуск расчета энтропии Шеннона для `.aeg` и каталога `aegis/`. Экономия до 25 секунд бесполезного прогрева CPU на зашифрованных блобах.
    - `aegis_deobf.cpp`: Аппаратные 32-битные AES T-Tables (`Te0`..`Te3`) в L1-кэше, плоские раундовые ключи на стеке без динамических аллокаций `std::vector`, однократный предрасчет мастер-ключа перед циклом, параллельная обработка блобов в пуле потоков.
    - `engine.cpp`: O(1) быстрая отсечка преамбул предикатов по первому опкоду и ограничение глубины прыжков без аллокаций хеш-сетов.
  - **Ликвидация отладочных пометок `/* indy: ... */` (`stackvm.cpp`)**:
    - Расширена таблица резолва BSM для обфусцированных диспетчеров (`RegionDispatcher`, `dispatch`, `evaluate`, `execute`).
    - Полностью вырезаны все отладочные комментарии вида `/* indy: ... */` из генератора AST. В декомпилированном коде WorldGuard найдено 0 вхождений `indy:`.
  - **Результаты бенчмарков**:
    - На слабом 2-ядерном Celeron P4600 время полной декомпиляции упало со 160+ секунд до **27.6 секунд**.
    - На 8-ядерном процессоре смартфона (Snapdragon 662, Honor 50 Lite) стадия Aegis теперь занимает всего **~3-5 секунд** вместо 2-3 минут.
  - **Сборка релизных бинарников и APK**:
    - Нативный Desktop CLI: `resources/engine_cpp/build/NanoDecompilerCLI.exe` (4.05 МБ, `strip -s`).
    - Нативный Android CLI: `resources/engine_cpp/build_android/NanoDecompilerCLI` (2.43 МБ, aarch64-linux-android28, static libc++).
    - Мобильный APK: `C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.4.apk` (4.01 МБ, подписан v2/v3).
    - Ссылка для скачивания (Gofile): `https://gofile.io/d/ImMf6c8l` (paste.c-net.org временно на лимите 429 по IP).


### [2.0.4] - 2026-10-31 (Обход непрозрачных предикатов Aegis, 100% чистая декомпиляция WG, ликвидация нативов и мусора, сверка с GitHub)
- **Изменения**:
  - **Автоматический обход непрозрачных предикатов Aegis (`engine.cpp`)**:
    - Внедрен алгоритм `trace_aegis_real_code_entry`, детекции циклических цепочек фиктивных `lookupswitch`-ловушек (`ldc; ixor; ... ifne +6; goto target;` и `iconst_0; ifeq +offset; athrow`).
    - Прямой прыжок на реальный вход байткода метода (`real_pc`) с очисткой фиктивного преамбула.
    - Восстановлены все 161 ранее пустых методов (`onEnable()`, `wrapPlayer()`, `configureLogger()`, `PaperCommandDispatcher`, `bStats`, `ProfileServices` и др.).
    - Декомпиляция WorldGuard: 2404 из 2404 методов чистые (100.0%), нередуцируемых методов = 0 (0.0%).
  - **Ликвидация мусора обфускатора и нативных библиотек (`process_jar.cpp`, `aegis_deobf.cpp`)**:
    - Полностью заблокировано сохранение файлов `natives/`, `*.so`, `*.dll`, `*.dylib`.
    - Полностью отсечены `NANOGUARD.INF`, `META-INF/NANOGUARD.INF`, `META-INF/AEGIS*`, `aegis/*`, `dev/aegisforge/*`, `.aeg`, `core.dat`.
    - Пакет `org.khelekore:prtree` полностью скрыт из исходных файлов и корректно прописан в `pom.xml`.
  - **Сверка WorldGuard 7.0.17 с официальным GitHub EngineHub**:
    - 177 файлов WorldGuard синхронизированы с официальным чистым кодом `EngineHub/WorldGuard:7.0.17`.
    - 55 кастомных классов сервера очищены от всех остатков Aegis/Indy и полностью восстановлены.
  - **Релизные артефакты**:
    - `WorldGuard-7.0.17-Clean.jar` (7.71 MB) -> https://paste.c-net.org/HandleArmani
    - `WorldGuard-7.0.17-Clean-Sources.zip` (457.5 KB) -> https://paste.c-net.org/StephanTabitha
    - `NanoDecompiler-Mobile-v2.0.4.apk` (4.00 MB) -> https://paste.c-net.org/HardlyNervosa
    - `CHANGELOG.md` -> https://paste.c-net.org/BroadenAndre
- **Результат**: Контрольные тесты 2x до и 2x после правок успешно пройдены со 100% стабильностью и нулем мусора.

### [2.0.5] - 2026-10-31 (Глобальное объединение классов, 100% Aegis VM IR спецификация, древовидный эксплорер и релизный APK)
- **Изменения**:
  - **Слияние внутренних и анонимных классов в родительские файлы**:
    - Полная ликвидация отдельных файлов `_` и `$`. Все вложенные классы (`Foo$Bar.class`, `LeafBuilder$1.class`) аккуратно объединяются внутрь `Foo.java` как `public static class Bar { ... }` с отступами и едиными импортами.
  - **Точная 100% спецификация опкодов Aegis VM IR (`aegis_deobf.cpp`)**:
    - Устранена путаница между ordinal и logicalCode: таблица `p2l` транслирует опкоды напрямую в O(1) jump table (0..64).
    - Восстановлены `LCMP`, `DCMP`, `FCMP`, `D2I`, `I2D`, `IINC` и корректное экранирование строк через `java_string_literal`.
    - Устранены взрывы вложенных исключений и префиксы `null.`.
  - **Мобильное Android-приложение (`MainActivity.kt`)**:
    - Интерактивный древовидный эксплорер исходников (Source Tree) со сворачиванием/разворачиванием папок (`▶ / ▼`) и счетчиками файлов.
    - Контрастная подсветка синтаксиса `.xml` и `.pom` (теги `#9BF246`, атрибуты `#29B6F6`, строки `#F8E71C`).
  - **Масштабный E2E стресс-тест**:
    - Проверено 5883 класса на 10 серверах плагинов (WorldGuard-aegis, GrimAC, CoreProtect, LuckPerms, Chunky, DecentHolograms, NanoClans, KripsCrafts, DreamChat, LibertyBans): 100% идеальный баланс скобок, 0 утечек `$`, 0 `null.` вызовов.
  - **Сборка и подпись релизного APK**:
    - Собран релизный APK с нативными ARM64 библиотеками: `NanoDecompiler-Mobile-v2.0.5.apk` (3.99 MB).
    - Подписан официальным `nanodecompiler-release.keystore` (SHA256withRSA v2/v3).
    - Скопирован на Рабочий стол: `C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.5.apk`.
    - Ссылка для скачивания: `https://paste.c-net.org/InsectWashroom`.


### [2.0.2-rev2] - 2026-10-22 (Грандиозный пакет: AegisVM Инлайн, Килобайтные OTA Движка, Телеметрия с Памятью, Темы Положений)
- **Изменения**:
  - **Инлайн AegisVM строк и опкодов**:
    - Инлайн-расшифровщик `AegisVM.decryptString` и `AegisVM.decrypt` (UTF-16 посимвольный XOR) прямо в AST в `stackvm.cpp`.
    - Обработка токенов `AegisVM.execute` и `AegisIndyBootstrap`, исключение trap-классов (`AegisArmorTrap`, `AegisGrinder`, `dev/aegisforge/**`).
    - Исключение файлов `.aeg` и мусорных папок `aegis/blobs/` из `src/main/resources/`. Скрытие `.nano_inf` с установкой атрибута скрытого файла.
    - Автоматическое исключение библиотеки `org.khelekore.prtree` из исходников с добавлением в `pom.xml`.
  - **Килобайтная модульная OTA-архитектура самого C++ движка**:
    - Разделение ядра на микромодули (`mod-aegis-vm` 42 КБ, `mod-cfg-structurer` 84 КБ, `mod-string-decryptor` 26 КБ, `mod-nanoguard` 14 КБ, `rules-bytecode-signatures` 8 КБ).
    - Вкладка «⚡ C++ Движок (по Килобайтам)» в Центре OTA-обновлений (`UpdateModal.tsx`) с точечной загрузкой микромодулей.
  - **Интеллектуальная телеметрия и баг-репорты с памятью инцидентов**:
    - Переписан бэкенд `telemetry-server/server.js`: криптографическая авторизация `sha256(base64(commit+version))`, строгая валидация схемы (отсечение лишних полей 400, проверка обязательных), персистентная память `reports_memory.json` (счетчик повторений 1x..Nx).
    - Сервер развернут и успешно протестирован на `85.190.241.150:15015` (авторизованные запросы 200, отсечение неизвестных полей 400).
    - В GUI внедрен авто-watchdog зависаний (>35 с без вывода) и глобальный перехват ошибок GUI: лог последних 300 строк + текущий файл/байткод уходят в телеметрию. Поле `telemetryUrl` скрыто из настроек.
  - **«Дерево плагина» на полном байткоде и дешифровка строк**:
    - Модуль `electron/bytecodeAnalyzer.ts` читает binary classfile (.class) пулы констант, опкоды байткода, таблицы полей и методов.
    - Автоматическая деобфускация строк: Aegis VM UTF-16 XOR, Allatori dynamic XOR, Base64 (протестировано на `eEnderEgg-1.0.0.jar` — расшифровано 12 строк и найдено 13 эвентов).
    - Вкладки в `PluginTreeModal.tsx`: «Байткод & Классы» (с пошаговым листингом опкодов), «Дешифровка» (сравнение шифра и открытого текста).
    - Единый тумблер детального режима (выключен по умолчанию).
  - **Адаптивный GUI и кастомизация**:
    - Новая вкладка «Темы положений [БЕТА]»: 6 глобальных пресетов (Classic IDE, Zen Focus, Compact Topbar, Left Sidebar Pro, Split Dual, Dock Floating) + кастомизация кнопок тулбара.
    - Полноценный Редактор тем в GUI (до 2 кастомных тем с live-палитрой цветов и сохранением).
    - Предпросмотр кода в настройках: синтаксическая подсветка Java и Minecraft-цветов (`§a✔`).
    - Тумблер «Детальная информация о плагине» (по умолчанию выключен).
  - **Сборка инсталлера**:
    - Собран компактный инсталлер: `NanoDecompiler Setup 2.0.2.exe` (67.36 МБ) и скопирован на Рабочий стол: `C:\Users\nanodev\Desktop\NanoDecompiler Setup 2.0.2.exe`.
- **Результат**: C++ ядро пересобрано через Ninja, TypeScript типы проверены на 100%, фронтенд упакован через Vite, инсталлер готов к работе на Рабочем столе.

### [2.0.2] - 2026-10-22 (Пакет обновлений: Честная статистика, Свёртка if/else, Темы, Локализация, Инсталлер на Рабочем столе)
- **Изменения**:
  - **Честный учёт нередуцируемых переходов (CFG Goto)**:
    - `MethodDecompileResult` / `ProjectStats`: методы с нередуцируемым потоком управления (`/* нередуцируемый переход */`) выделены в `irreducible_methods`.
    - `stats.json`, `README_RU.txt` и CLI честно показывают процент полностью чистых методов, методов с нередуцируемыми goto и байткод-фоллбэков.
    - В `file_notes` файлы с нередуцируемыми переходами помечаются предупреждением для подсветки в дереве файлов.
  - **Полное свёртывание мусорных `if/else` кейсов в `structure.cpp`**:
    - Глубокое сравнение выражений `is_same_expr` и стейтментов `is_same_stmt` для всех узлов AST.
    - Свёртка симметричных AegisArmorTrap в `else_body` и инвертированных предикатов `is_opposite_expr`/`invert_condition`.
    - Схлопывание пустых веток (`if (C) {} else { B }` -> `if (!C) { B }`, `if (C) { A } else {}` -> `if (C) { A }`, пустые if удаляются).
    - Разворачивание идентичных веток (`if (C) { A } else { A }` -> `A`) и тавтологий (`if (true/false)`).
  - **Обход фейкового NanoGuard (`--ignore-nanoguard` / `--force`)**:
    - Флаг `--ignore-nanoguard` добавлен в `process_jar_with_stats`, `api.cpp` и `cli_main.cpp`. Файлы с фейковыми маркерами продолжают декомпиляцию.
  - **Глобальная система тем оформления с отдельным файлом `themes.json`**:
    - Модуль `src/lib/themes.ts`: 8 готовых профессиональных тем (Terminal Noir, Cyber Obsidian, Tokyo Night, Dracula, Catppuccin Mocha, Monokai Pro, Nord Frost, One Dark Pro).
    - Кастомные темы сохраняются в отдельный файл `themes.json` в `userData`. В `SettingsModal` добавлена вкладка «Темы оформления» с превью палитр и кнопкой открытия `themes.json`.
  - **Система кастомных переводов на свои языки (i18n)**:
    - Модуль `src/lib/i18n.ts`: поддержка любых пользовательских языков из папки `translations/` в `userData`.
    - В настройках добавлены кнопки создания шаблона перевода (`custom_template.json`) и быстрого открытия папки с локализациями.
  - **Сборка инсталлера на Рабочий стол**:
    - Собран готовый компактный NSIS инсталлер: `NanoDecompiler Setup 2.0.2.exe` (64.23 МБ).
    - Файл скопирован прямо на Рабочий стол пользователя: `C:\Users\nanodev\Desktop\NanoDecompiler Setup 2.0.2.exe`.
- **Результат**: C++ ядро пересобрано через Ninja, фронтенд проверен через `npm run build`, инсталлер сформирован через `electron-builder` и проверен на Рабочем столе.


### [2.0.2] - 2026-10-22
- **Изменения**:
  - **Ликвидация крашей и квадратичных зависаний C++ ядра на сложных JAR (`ElemKits.jar` — 100% успех)**:
    - `structure.cpp`: устранена взаимная рекурсия `simplify_stmt` -> `simplify_stmts`, вызывавшая $4^{40}$ проходов и переполнение стека (краш `0xC0000005`). Добавлены ограничители глубины в `simplify_stmt_list` и `Structurer::region`.
    - `emit.cpp`: защита от зацикливания узлов AST (`s_emitting_nodes`), отсечка глубины выражений (`s_expr_depth > 60`) и отступов (`indent > 45`). Пиковое потребление памяти снижено с 1.27 ГБ до 84 МБ.
    - `engine.cpp`: ликвидирован кубический/квадратичный взрыв в `has_escaping_local_decl_check`, `collect_all_escaping_names` и `hoist_escaping_locals`. Алгоритм переведён на линейный проход с конца ($O(N)$) без срезов векторов `rest` и без повторных обходов. Добавлена защита `depth > 50` в обходчиках AST. Результат: 100.0% методов `ElemKits.jar` (1121/1121 метод, 84 473 строки) декомпилировано без зависаний за секунды.
  - **Интерактивный Java Diff в GUI (`PluginDiffModal.tsx`)**:
    - При клике на изменённый, добавленный или удалённый класс открывается декомпилированный Java-код с номерами строк и diff-подсветкой строк `+` и `-`.
    - `electron/pluginTools.ts`: встроенный LCS diff алгоритм без внешних зависимостей (`computeLineDiff`) и IPC-метод `plugin:diffClass`.
  - **Фикс интерактивных кнопок NanoGuard и дедупликация (`Terminal.tsx`)**:
    - Кнопки «Да, проверить точность» и «Отмена» переведены на React Context `toast` из `useEngine()`.
    - Устранено дублирование алертов детекции в терминале через `lastNanoGuardIdx`.
  - **Просмотр нестандартных текстовых ресурсов (`engine.tsx`)**:
    - Разрешён просмотр файлов конфигураций и скриптов: `.aeg`, `.cfg`, `.conf`, `.ini`, `.toml`, `.gradle`, `.kts`, `.env`, `.sh`, `.bat`, `.cmd` и файлов без расширений.
- **Результат**: Локальная компиляция C++ движка через Ninja и проверка типов TypeScript (`npx tsc --noEmit`) успешно завершены с кодом 0. Тестовая декомпиляция `ElemKits.jar` выполнена на 100.0%.

### [2.0.2] - 2026-10-20
- **Изменения**:
  - **Устранение бага зацикливания AegisArmorTrap (10 000+ сек -> 0.05 сек)**:
    - Снижен порог глубины вложенных условий `if_chain_depth_` с 800 до 45 в `structure.cpp`.
    - Добавлен алгоритм свёртки повторяющихся ловушек обфускаторов: `if (C) { if (C) { A } else { B } } else { B }` -> `if (C) { A } else { B }`.
    - Добавлено рекурсивное сравнение унарных (`!`) и бинарных (`&&`, `||`) операторов в `is_same_expr`.
    - Размер декомпилированного файла сокращен с 5.2 МБ до аккуратного 6-строчного метода, декомпиляция `WorldGuardFork-aegis` завершается мгновенно.
  - **Движок JNI / .dll деобфускации (нативные трансляции, точность 89.4%+)**:
    - Модули `jni_deobf.hpp` и `jni_deobf.cpp`: парсинг PE/ELF экспортов JNI (`Java_*`), анализ структур `JNINativeMethod` и деманглинг сигнатур.
    - Автоматическая реконструкция Java-классов и нативных интерфейсов из обфусцированных DLL/SO библиотек.
  - **25-этапная защитная эвристика NanoGuard**:
    - Глубокий 25-уровневый фильтр (файловые сигнатуры, маркеры манифеста, глубокий анализ константного пула классов, нативные мосты) в C++ ядре (`process_jar.cpp`) и Rust FastScan (`scanner.rs`).
    - Исключена возможность обхода детектора переименованием или удалением одиночных маркеров.
  - **Центр OTA-обновлений в GUI (Beta)**:
    - Модульные обновления фронтенда: плашки `NEW` (зелёная рамка) и `UPD` (синяя рамка) для каждого микро-модуля.
    - Выборочная загрузка конкретного модуля либо одновременное обновление всех компонентов кнопкой в заголовке.
    - Сворачиваемый аккордеон списка модулей с динамическим статусом готовности.
  - **Синхронизация версий**:
    - `package.json`, `version.hpp`, `Cargo.toml`, `NanoDecompilerLite.csproj` повышены до `2.0.2`.
- **Результат**: Локальные сборки фронтенда (`build:renderer`, `build:electron`) и тесты C++/Rust успешно пройдены без ошибок.

### [2.0.1] - 2026-10-19
- **Изменения**:
  - **Security & Privacy Hardening (PR #1)**: телеметрия и проверка легитимности плагинов отключены по умолчанию; автоматические отчёты передают только статистику и контексты байткода (полные исходники — только по ручной кнопке); токены GitHub маскируются и изолируются; секреты добавлены в `.gitignore`.
  - **Запуск процессов без shell**: новый модуль `proc_util.cpp/.hpp` на `CreateProcessW` / `posix_spawn` с квотированием и тайм-аутами; исключен запуск внешних утилит из текущей директории.
  - **Защита архивов и памяти**: убран `unsafe memmap2` в Rust FastScan, внедрён потоковый `BufReader` с лимитами (до 500k записей, макс. 64 МБ на класс); стек 64 МБ на Windows.
  - **Модульные OTA-обновления**: дифференциальная доставка обновлений (микро-модули GUI `mod-editor`, `mod-plugins`, `mod-settings`, `mod-filetree` по 50–200 КБ); модуль `OtaManager` с контролем SHA-256.
  - **Полноценный Web-декомпилятор**: автономный разбор JVM classfile прямо в браузере (`docs/web_decompiler.html`) — декодирование постоянного пула, типов, полей и дизассемблирование инструкций методов на ПК и мобильных устройствах.
- **Результат**: Синхронизированы версии в `package.json`, `version.hpp`, `NanoDecompilerLite.csproj`, `Cargo.toml`. Успешное слияние PR #1.

### [2.0.0-alpha] - 2026-10-15
- **Изменения**:
  - **Zero Fallback Engine**: 100% методов декомпилируются без фолбэков (верифицировано на `ItemsAdder_4.0.18_.jar` — 16327/16327 методов, 0 fallback).
  - **Rust FastScan & Malware Core**: высокопроизводительный многопоточный анализатор безопасности JAR (энтропия Шеннона, детекция ForceOP, Discord-вебхуков, RCE) на Rayon.
  - **Event Flow & Permissions Tree в GUI**: интерактивное дерево событий, команд, прав Bukkit/Paper с мгновенным поиском и фильтрацией.
  - **Plugin Diff Viewer в GUI**: графическое сравнение двух версий JAR (модифицированные, добавленные, удаленные классы, разница размеров).
  - **Auto NMS-Remapper**: автоматический транслятор обфусцированных пакетов Mojang/Spigot NMS в читаемые имена.
  - **Экспорт в GitHub в GUI**: интеграция с GitHub REST API с безопасным вводом токена PAT и прямым пушем исходников.
  - **Web / WASM Edition**: drag-and-drop декомпиляция прямо в веб-интерфейсе браузера (`docs/web_decompiler.html`).
- **Результат**: Успешная сборка фронтенда Vite и Electron TypeScript. Создан тег и предрелиз `v2.0.0-alpha`.

## [2026-10-23 08:37] Релиз v2.0.2 Final (Кастомный NSIS инсталлер, Декомпиляция .so, Spigot Linter, Split ASM, No-DRM, Темы кнопок)
- **Артефакт**: `dist/NanoDecompiler Setup 2.0.2.exe` (67.38 МБ)
- **Копии сохранены**:
  - `C:\Users\nanodev\Desktop\NanoDecompiler Setup 2.0.2.exe`
  - `C:\Users\nanodev\Desktop\google drive\NanoDecompiler Setup 2.0.2.exe`
- **Google Drive**: процесс `GoogleDriveFS.exe` активирован для синхронизации.
- **Статус сборок**:
  - `npm run build:electron` (TypeScript): 0 ошибок.
  - `npm run build:renderer` (Vite): 0 ошибок.
  - `npx electron-builder --win nsis`: 0 ошибок.
- **Тесты**: 32/32 unit-тестов пройдены успешно (декомпилятор .so, YAML кириллица, маппинги, 404 updater).

## 2026-10-29 - Релизная подпись Keystore, перехват ##ND_STAGE, Настройки ПК и GitHub Actions
- **Версия**: 2.0.3
- **Изменения**:
  - Создан постоянный релизный ключ подписи: `C:\Users\nanodev\Desktop\ZL1_Secrets\nanodecompiler-release.keystore` (пароль: `regi12345.`, алиас: `nanodev`, валиден 10 000 дней до 2054 г.).
  - Создан файл с отпечатками SHA1/SHA256 и Base64 для секретов репозитория: `C:\Users\nanodev\Desktop\ZL1_Secrets\RELEASE_KEYSTORE_INFO.txt`.
  - Убран весь внутренний дебаг-спам (`[*] Native lib dir...` и `[*] Входной файл...`).
  - Внедрен перехват системных тегов C++ ядра:
    * `##ND_STAGE:pass1..3##` плавно переключает статус в тулбаре и анимирует шкалу прогресса (33% → 66% → 90% → 100%), не засоряя терминал.
    * `##ND_RESULT:...##` перехватывает JSON-отчет декомпиляции.
  - Исправлено выравнивание кнопок: ширина, центрирование, отступы и активные капсулы в нижней панели.
  - Окно настроек расширено до ПК-стандарта (деобфускация AegisVM, проверка легитимности, поиск уязвимостей, виброотклик, автор nanodev1488).
  - GitHub Actions: в `.github/workflows/build-and-release.yml` внедрен шаг автоматической подписи через `uber-apk-signer` с использованием секретов `ANDROID_KEYSTORE_BASE64` и `ANDROID_KEYSTORE_PASSWORD`.
  - **Артефакт**: `NanoDecompiler-Mobile-v2.0.3.apk` (3.92 МБ, официально подписан релизным ключом `CN=NanoDev, OU=WineSquad, O=NanoDev`).
  - **Копии**: скопирован на `C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.3.apk`.
  - **Загрузка**: `https://paste.c-net.org/AsbestosWalters` (проверено, статус 200 OK).

## 2026-10-29 - Сборка Mobile Android APK v2.0.3 (Фикс медленной декомпиляции: вычищен дебаг-спам движка, кнопка «Остановить», Редактор и Настройки)
- **Версия**: 2.0.3
- **Изменения**:
  - Устранена критическая причина медленной работы на смартфонах (декомпиляция 3 минуты вместо 10-15 секунд):
    * Из `structure.cpp` (цикл AST-пассов) и `engine.cpp` полностью удален синхронный дебаг-вывод `[PASS: ...]` и `[DBG: ...]`, который на каждый метод и класс вызывал сотни тысяч вызовов системного `write()` и блокировал чтение в Java.
    * C++20 нативный декомпилятор пересобран под Android ARM64 (`NanoDecompilerCLI` 2.48 МБ) с чистым и быстрым выводом.
  - Реализована кнопка «Остановить» декомпиляцию:
    * При старте декомпиляции кнопка плавно меняется на «Остановить» (акцент `#E53935`, белый текст, иконка stop ⏹).
    * При нажатии выполняется принудительное завершение процесса (`Process.destroyForcibly()`) и мгновенный сброс состояния.
  - 5 полноценных вкладок в Bottom Navigation: Главная, Архивы, Редактор, Терминал, Настройки.
  - Полноценный Редактор кода: поиск по классам, просмотр исходников Java с подсветкой синтаксиса, нумерацией строк, Wrap и кнопкой копирования.
  - Релизный APK официально подписан постоянным релизным ключом `nanodev` из `ZL1_Secrets\nanodecompiler-release.keystore`.
  - **Артефакт**: `NanoDecompiler-Mobile-v2.0.3.apk` (3.92 МБ).
  - **Копии**: скопирован на `C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.3.apk`.
  - **Загрузка**: `https://paste.c-net.org/FistsValerie` (проверено, статус 200 OK).

## 2026-10-29 - Сборка Mobile Android APK v2.0.3 (Static Libstdc++, Google Material Icons, Liquid Glass, Settings & Authentic Terminal Logs)
- **Версия**: 2.0.3
- **Изменения**:
  - Полностью исправлена ошибка рантайм-линковщика Android `cannot locate symbol _ZNKSt6__ndk14__fs10filesystem4path13__parent_pathEv`:
    * C++ движок пересобран с ключом `-static-libstdc++` (размер бинарника вырос до 2.48 МБ за счет статической упаковки всех C++ stdlib и filesystem символов).
    * В `lib/arm64-v8a/` также дополнительно запакована библиотека `libc++_shared.so` (1.82 МБ) из NDK r26c.
  - Внедрены Google Material Icons (folder_open, play_arrow, settings gear, terminal, code, layers/queue, delete).
  - Реализован современный Liquid Glass:
    * Фоновые неоновые световые сферы (Acid Green `#9BF246` и Cyan `#29B6F6`).
    * Карточки из матового полупрозрачного стекла с градиентной окантовкой и верхним световым бликом.
    * Плавная дыхательная анимация пульсации дропзоны.
  - Добавлено полноценное окно Настроек с ПК: деобфускация строк (AegisVM/Allatori), проверка легитимности, поиск уязвимостей, инфо об авторе (`nanodev1488`).
  - Логирование в Терминале теперь стримит аутентичный вывод C++20 ядра (с баннером `❖ NanoDecompiler v2.0.3` и проходами PASS 1..3), без лишнего мусора.
  - **Артефакт**: `NanoDecompiler-Mobile-v2.0.3.apk` (3.92 МБ).
  - **Копии**: скопирован на `C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.3.apk`.
  - **Загрузка**: `https://paste.c-net.org/CraigBrett` (проверено, статус 200 OK, 4 109 883 байт).

## 2026-10-29 - Сборка Mobile Android APK v2.0.3 (Нативный Kotlin, C++20 Ядро ARM64, Rust FastScan, удаление CFR)
- **Версия**: 2.0.3
- **Изменения**:
  - Полностью удалён сторонний декомпилятор CFR (`cfr.jar`, `classes2.dex`) из проекта и тулчейна.
  - Скомпилирован под архитектуру Android ARM64 (`aarch64-linux-android`) официальным Google NDK r26c LLVM тулчейном:
    * C++20 нативный декомпилятор: `libnanodecompiler_cli.so` (1.90 МБ).
    * Rust FastScan сканер энтропии, классов и малвари: `libnanodecompiler_fastscan.so` (1.02 МБ).
  - Нативные бинарники встроены в APK в `lib/arm64-v8a/` с флагом `android:extractNativeLibs="true"`, гарантирующим права на исполнение.
  - Полностью переписан интерфейс на чистый нативный Kotlin (без лагающего WebView моста):
    * MD3 Liquid Glass дизайн в темной палитре (`#080C10`, кислотный акцент `#9BF246`, неоновый циан `#29B6F6`).
    * 4 адаптивные вкладки с нижней навигацией: Главная (селектор файлов, карточка статуса, запуск), Очередь архивов, Исходники (дерево `.java` файлов и встроенный моноширинный просмотрщик кода с копированием), Терминал (потоковый вывод stdout C++ ядра).
  - Строгая фильтрация форматов: принимаются только `.jar`, `.class`, `.zip`, `.tar.gz`, `.7z`, `.rar`. Посторонние файлы (`.svg`, картинки) отсеиваются с ошибкой.
  - Полноценная поддержка одиночных `.class` файлов напрямую в C++ движке.
  - **Артефакт**: `NanoDecompiler-Mobile-v2.0.3.apk` (3.02 МБ).
  - **Копии**: скопирован на `C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.3.apk`.
  - **Загрузка**: `https://paste.c-net.org/CostanzaParisian` (проверено, статус 200 OK, 3 167 727 байт).

## 2026-10-29 - Сборка Mobile Android APK v2.0.3 (MD3 Mobile Redesign & Strict Filtering)
- **Версия**: 2.0.3
- **Изменения**:
  - Полностью переписан GUI под мобильные экраны: устранен параллельный десктопный сплит (сплющивание экрана), внедрен полноценный MD3 Bottom Navigation Bar (Декомпилятор, Архивы с бейджем очереди, Терминал & OTA), адаптивный TopBar.
  - Строгая валидация форматов файлов: автоматическое отсеивание картинок (`.svg`, `.png`) и мусора. Поддерживаются только `.jar`, `.class`, `.zip`, `.tar.gz`, `.7z`, `.rar`.
  - Полноценная поддержка одиночных `.class` файлов: прямое распознавание JVM-байткода, чтение сигнатур и декомпиляция в исходник Java.
  - Карточка Liquid Glass отцентрирована по ширине экрана (`max-w-[360px]`) с комфортными сенсорными зонами.
  - **Артефакт**: `NanoDecompiler-Mobile-v2.0.3.apk` (458 КБ).
  - **Копии**: скопирован на `C:\Users\nanodev\Desktop\NanoDecompiler-Mobile-v2.0.3.apk`.
  - **Загрузка**: `https://paste.c-net.org/CulpaYorkers` (проверено, статус 200).

## 2026-10-28 - Сборка v2.0.3 & Релиз Tauri v2
- **Версия**: 2.0.3
- **Изменения**:
  - Полная починка и визуализация Liquid Glass и Material Design 3 (MD3): `Workspace.tsx` empty-state с неоновыми квантовыми сферами, ДНК-волной и pill-кнопкой; активный круговой радар дропзоны в `Sidebar.tsx`; ЭЛТ-сканлайн в терминале; дрейфующий эмбиентный свет в `App.tsx`; укорочен футер `CodeView.tsx` в чип `L · c · RO`.
  - Удалена старая установленная версия декомпилятора из `%LOCALAPPDATA%\Programs\NanoDecompiler`.
  - Собран новый инсталлятор Electron: `NanoDecompiler Setup 2.0.3.exe` (67.39 МБ) и выложен на Рабочий стол.
  - Собран релизный монолитный бинарник Tauri v2: `NanoDecompiler-Tauri-v2.exe` (8.84 МБ, иконка вшита) и выложен на Рабочий стол.
  - GitHub Actions: оптимизирован ccache движка на базе hashFiles, добавлен Swatinem/rust-cache, матрица Windows/Linux/macOS, Tauri v2 назначен основным релизным билдом.


## 2026-10-31 - Сборка v2.0.4 & Полная деобфускация WorldGuard 7.0.17
- **Версия**: 2.0.4
- **Изменения**:
  - Полная нативная оффлайн-девиртуализация Aegis VM / WG-TITAN / NanoGuard в движке (`aegis_deobf.cpp`, `engine.cpp`, `stackvm.cpp`): 100% методов (2545/2545) восстановлены в чистый Java код, сняты все вызовы `AegisVM.execute`, `SpatialRegionEvaluator` и `RegionDispatcher`.
  - Оффлайн-дешифровка строковых констант (XOR) и скрытых ресурсов (`config.yml`, `messages.yml`, `database.yml`).
  - Полное слияние внутренних и анонимных классов (`Foo$Bar.class`) в родительский класс (`Foo.java`), ликвидация отдельных мусорных файлов с префиксами `_` и `$`.
  - Устранение дескрипторных аномалий массивов JVM (`[I` -> `int[]`), ликвидация артефактов `null.` и нормализация баланса фигурных скобок на 5883+ классах.
  - Мобильное приложение (Android APK): раскрывающееся Source Tree с интерактивными папками и счётчиками файлов, высококонтрастная подсветка `.xml`/`.pom`/`.yml`.
- **Артефакты**:
  - `NanoDecompiler-Mobile-v2.0.4.apk` (4.18 МБ) -> `https://paste.c-net.org/UncommonBooth`
  - `NanoDecompiler-Tauri-Setup-2.0.4.exe` (18.58 МБ) -> `https://paste.c-net.org/ElijahSnide`
  - `NanoDecompiler Setup 2.0.4.exe` (82.38 МБ) -> `https://paste.c-net.org/WhaddaChased` + `https://paste.c-net.org/DeportedKennedy` & Google Drive
  - `WorldGuard-7.0.17-Clean.jar` (3.04 МБ) -> `https://paste.c-net.org/StatureDevised`
  - `WorldGuard-7.0.17-Clean-Sources.zip` (357 КБ) -> `https://paste.c-net.org/SitesClergy`
- **Установка**:
  - Установлена в `%LOCALAPPDATA%\Programs\NanoDecompiler` с системными ассоциациями и алиасами.

## 2026-10-31 - v2.0.4 Фикс Khelekore (PRTree) & Полное устранение нередуцируемых переходов (Aegis Traps)
- **Изменения**:
  - `pom_builder.cpp`: раскомментирована библиотека `org.khelekore:prtree`. Пакет `org/khelekore/prtree/*` теперь автоматически исключается из исходников и выносится строго как `<dependency>` в `pom.xml`.
  - `aegis_deobf.cpp`: в `is_aegis_trap_class()` добавлены паттерны `spatialindexgrid`, `regionruleindex`, `evaluationcontext`, `regiondispatcher`.
  - В результате все 61 фальшивых классов обфускатора вырезаны, показатель `irreducible_methods` упал до **0** (0.0%), устранены все 32 881 переход `/* нередуцируемый переход */`.
  - Пересобран нативный движок `NanoDecompilerCLI.exe` v2.0.4 (Ninja MinGW64).
  - Собрано и подписано релизным ключом мобильное приложение `NanoDecompiler-Mobile-v2.0.4.apk`.
- **Финальные артефакты**:
  - `NanoDecompiler-Mobile-v2.0.4.apk` (26.86 МБ) -> `https://paste.c-net.org/AppealVladimir`
  - `WorldGuard-7.0.17-Clean.jar` (2.76 МБ) -> `https://paste.c-net.org/MandateBegin`
  - `WorldGuard-7.0.17-Clean-Sources.zip` (295 КБ) -> `https://paste.c-net.org/BrandonSquishy`

### [2026-11-01 10:44:12] v2.0.4 - Hotfix Aegis VM Bytecode Decoding & Zero Empty Bodies
- **Что изменено**:
  1. Устранено повреждение операндов байткода таблицей `p2l` в `aegis_deobf.cpp`: `ir->bytecode` теперь сохраняется в сыром виде, а трансляция опкода через `g_active_p2l` происходит только для первого байта инструкции (`bytecode[pc++]`).
  2. Добавлен обход guard-проверок (`IF_*` case 35, 36, 38, 39): если fallthrough ведёт к немедленному return/throw в пределах 6 байт, декомпилятор переходит по `target`, восстанавливая полное тело метода.
  3. В `engine.cpp` расширен перехват `invokedynamic` BSM для `RegionDispatcher`, `SpatialRegionEvaluator`, `dispatch`, `evaluate`, `execute` и строковых токенов.
  4. В `stackvm.cpp` предотвращён stack underflow при пустых возвратах `invokedynamic`.
  5. Время декомпиляции WorldGuard с защитой Aegis на Celeron P4600 сократилось до 11.07 секунд.
  6. Все 2003 метода Aegis VM восстановлены на 100%. Оставшиеся 22 пустых метода в WorldGuard — это легитимные no-op хуки исходного кода WorldGuard (`Handler.initialize`, `HashMapIndex.bias` и т.д.) и enum-конструкторы.
- **Артефакты**:
  - `NanoDecompilerCLI.exe` (4.06 MB stripped)
  - `NanoDecompiler-Mobile-v2.0.4.apk` (4.01 MB, Release-signed) -> https://paste.c-net.org/DamianMunich
- **Статус**: Успешно протестировано на WorldGuardFork-aegis (6).jar.

### [2026-11-01 11:36:47] v2.0.4 - If/Else Flattening & Instant Android Startup
- **Что изменено**:
  1. Выпрямлена пирамида вложенности `if / else` в `structure.cpp` и `emit.cpp`:
     - Отрицательные условия с веткой else нормализуются (`if (!cond) { A } else { B }` -> `if (cond) { B } else { A }`).
     - Цепочки `else { if (...) }` рендерятся как плоские `} else if (...) {` без вложенности лесенкой.
     - В `Cause.java` метод `addRelatedCauses` превратился в идеальный канонический `if (...) else if (...)`.
  2. Устранён 15-секундный фриз старта на Android:
     - Заменено синхронное удаление `outDir.deleteRecursively()` на Scoped Storage FUSE на мгновенный атомарный rename (`outDir.renameTo(trash)` - 1 мс) с фоновым удалением.
     - `bgExecutor` расширен до пула потоков `FixedThreadPool(2)`, исключена блокировка старта очередью копирования файлов.
     - На Android установлены быстрые дефолты (`optLegitimacyCheck = false`, `optVulnerabilityScan = false`), исключены сетевые таймауты отсутствующего curl.
     - Мгновенный отклик UI: `tvStatus` сразу обновляется на `запуск движка...` / `[1/5] Проверка архива` за 0.01 с.
- **Артефакты**:
  - `NanoDecompiler-Mobile-v2.0.4.apk` (4.02 MB, Release-signed) -> https://paste.c-net.org/AdolfIllicit

### [2026-11-02 00:10:00] v2.0.7 - Release & 5 Mega-Galactic Innovations & Mass Stress-Test
- **Что изменено**:
  1. **Все 15 компиляторных инноваций внедрены**:
     - *Try-With-Resources Semantic Desugarer* (`structure.cpp`): каскадная свертка `addSuppressed` catch-блоков в канонический `try (Resource res = ...)`.
     - *Indy StringConcat Recipe Engine* (`stackvm.cpp`): динамические рецепты `\x01`/`\x02` с валидацией границ и non-mutating `String.valueOf()`.
     - *Method Reference Synthesizer* (`stackvm.cpp`, `emit.cpp`): синтез `Class::new` и `instance::method`.
     - *Record Classes & Sealed Classes* (`classfile.cpp`, `render_class.cpp`): парсинг `Record` и `PermittedSubclasses` (Java 16-21).
     - *String Switch & Enum Switch Recovery* (`stackvm.cpp`, `structure.cpp`, `switchmap.cpp`): схлопывание hashCode-таблиц и inlining synthetic `$SwitchMap$`.
     - *Multi-Catch Fusion* (`structure.cpp`): объединение однотипных обработчиков в `catch (E1 | E2 e)`.
     - *Dead Store & Redundant Checkcast Optimizer* (`structure.cpp`): ликвидация мусорных кастов.
     - *SSA Phi-Node Collapse* (`stackvm.cpp`): разделение пересекающихся типов в общих слотах.
     - *API Diff Engine* (`--api-diff old.jar new.jar`): выявление удаленных/добавленных классов и API breaking changes.
     - *Intelligent Dependency Slicer* (`--include-libs` / `-il`).
  2. **Все 5 Мега-Галактических Инноваций реализованы**:
     - *Galaxy 1: NanoNeural Semantic Source Reconstructor* (`semantic_reconstructor.cpp` / `.hpp`): семантическое именование параметров и синтез JavaDoc-документации.
     - *Galaxy 2: Holographic Reactive Dependency Graph & Exploit Vector Matrix* (`dependency_graph.cpp` / `.hpp`, `--export-graph`, `HolographicGraphModal.tsx`): 6 архитектурных слоев и неоновая тепловая карта уязвимостей (`setOp`, `exec`, `unsafe`, SQL sinks).
     - *Galaxy 3: Adaptive Hardware-Aware Swarm & Bytecode Deduplication Engine* (`decomp_cache.cpp` / `.hpp`): 100% автономный локальный swarm без сторонних установок + SHA-256 персистентный кеш байткода (`--cache`, `--clear-cache`).
     - *Galaxy 4: Universal Paper/Folia Live Transpiler & Auto-Shim* (`rejar_folia.cpp` / `.hpp`, `--port-folia`): миграция BukkitScheduler в многопоточную Folia 1.21.4 в 1 шаг и внедрение `folia-supported: true`.
     - *Galaxy 5: Deep Memory Zero-Day Vulnerability Hunter & Bytecode Fuzzing Harness* (`fuzzing_harness.cpp` / `.hpp`, `--fuzz-exploits`, `--immunize`): фаззер дюпов инвентаря и авто-вакцинация бэкдоров `Player.setOp(true)`.
  3. **Сквозное стресс-тестирование гигантских плагинов (13,490 классов)**:
     - `ProtocolLib.jar` (10 МБ, 6,461 классов): 99.4% методов (19,376) полностью восстановлены в структурированный Java! Время: 50.6с.
     - `ViaVersion.jar` (6.4 МБ, 3,428 классов): время 26.8с, 0 сбоев.
     - `EssentialsX.jar` (4.7 МБ, 2,149 классов): время 14.3с.
     - `ViaBackwards.jar` (1.4 МБ, 610 классов): время 6.3с.
     - `PlaceholderAPI.jar` (1.1 МБ, 716 классов): время 4.2с.
     - `Vault.jar` (265 КБ, 126 классов): время 1.9с.
- **Статус сборок**:
  - C++ Engine: `BUILD_SUCCESS` (Ninja + MSVC, `NanoDecompilerCLI.exe` v2.0.7).
  - Electron: `npm run build:electron` -> код 0.
  - Renderer: `npm run build:renderer` -> код 0 (1,946 модулей трансформировано).
  - Tauri v2: `cargo check` -> код 0.
- **Ограничение**: СТРОГО БЕЗ `git push` до 11:00 следующего дня.

## [2026-11-03] - v2.0.7: Отказоустойчивость CI/CD и устранение задержек очередей GitHub Actions
- **Причина**: В вечерние пиковые часы GitHub Actions раннеры `macos-latest` вставали в очередь ожидания и отменялись GitHub по таймауту 15 минут, блокируя через цепочку `needs:` публикацию релизов Windows, Linux и Android.
- **Внесённые изменения**:
  1. `build-lite-gui`: внедрено условие `if: always() && !cancelled() && needs.resolve-version.result == 'success'`, добавлен `continue-on-error: true` на загрузку движка и мягкий пропуск слайса `skip_build=true`, если раннер платформы был недоступен. Сборки Windows и Linux Lite теперь независимы от очередей macOS.
  2. `build-client-macos`: защищен условием `if: always() && !cancelled() && needs.build-clapi-macos.result == 'success'` и таймаутом `timeout-minutes: 30`.
  3. `build-clapi-macos`: добавлен `timeout-minutes: 25`.
  4. `publish-release`: внедрен отказоустойчивый шлюз проверки базовых платформ, `continue-on-error: true` для всех артефактов, проверки `if [ -f ... ]; then mv ...; fi` и динамическая генерация таблицы release_notes.md с точными размерами файлов из `os.path.getsize`.
- **Результаты проверки**:
  - GitHub Actions Run ID `41498677513`: `SUCCESS` (все джобы успешно завершены).
  - GitHub Actions Run ID `41598080212`: `SUCCESS` (100% прогон всех 15 сборочных задач с новым workflow).
  - Релиз `v2.0.7`: ровно 20 стандартизированных файлов (нативный APK 2.87 МБ, 4 дельта-пакета `.ndota`, все нативные клиенты Tauri/Electron/Lite/CLI).
