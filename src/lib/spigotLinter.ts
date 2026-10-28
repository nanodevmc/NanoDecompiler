// src/lib/spigotLinter.ts - Статический анализатор частых багов, утечек памяти
// и крашей в коде плагинов Minecraft (Spigot, Paper, Purpur, Bukkit).

export interface LintIssue {
  line: number;
  severity: "critical" | "warning" | "info";
  ruleId: string;
  title: string;
  message: string;
  suggestion: string;
}

export function lintSpigotJavaSource(sourceCode: string): LintIssue[] {
  const lines = sourceCode.split("\n");
  const issues: LintIssue[] = [];

  let inAsyncBlock = false;
  let asyncBlockDepth = 0;
  let hasListenerInterface = false;

  // Быстрая проверка декларации класса
  for (let i = 0; i < Math.min(lines.length, 50); i++) {
    if (/implements\s+.*Listener/i.test(lines[i])) {
      hasListenerInterface = true;
      break;
    }
  }

  for (let i = 0; i < lines.length; i++) {
    const lineNum = i + 1;
    const line = lines[i];
    const trimmed = line.trim();

    // Пропуск однострочных комментариев
    if (trimmed.startsWith("//") || trimmed.startsWith("/*") || trimmed.startsWith("*")) {
      continue;
    }

    // 1. Утечка памяти Player в статических полях
    if (/\bstatic\s+.*\b(?:Player|CraftPlayer)\b/i.test(line)) {
      if (/Map<.*Player/i.test(line) || /List<.*Player/i.test(line) || /Set<.*Player/i.test(line) || /static\s+Player\s+/i.test(line)) {
        issues.push({
          line: lineNum,
          severity: "critical",
          ruleId: "SPIGOT_STATIC_PLAYER_LEAK",
          title: "Критическая утечка памяти: статический Player",
          message: "Хранение Player в static полях или коллекциях приводит к утечке памяти после выхода игрока с сервера.",
          suggestion: "Замените Player на UUID (UUID.fromString / player.getUniqueId()) или используйте WeakReference.",
        });
      }
    }

    // 2. Отслеживание асинхронных блоков
    if (/runTaskAsynchronously|runAsync|CompletableFuture\.runAsync|Thread\(|new\s+Runnable/.test(line)) {
      inAsyncBlock = true;
      asyncBlockDepth = 0;
    }
    if (inAsyncBlock) {
      if (line.includes("{")) asyncBlockDepth++;
      if (line.includes("}")) {
        asyncBlockDepth--;
        if (asyncBlockDepth <= 0) inAsyncBlock = false;
      }

      // Вызовы синхронного Bukkit API из асинхронного потока
      if (/\b(?:teleport|setBlock|getBlockAt|spawnEntity|damage|dispatchCommand|setHealth|openInventory|kickPlayer)\b/.test(line)) {
        issues.push({
          line: lineNum,
          severity: "critical",
          ruleId: "SPIGOT_ASYNC_CRAFTBUKKIT_CALL",
          title: "Асинхронный вызов Bukkit API (Риск краша сервера)",
          message: "Вызов методов изменения мира или сущностей из асинхронного потока приводит к повреждению чанков или Asynchronous entity track exception.",
          suggestion: "Оберните вызов в Bukkit.getScheduler().runTask(plugin, () -> { ... }) для выполнения в главном тике.",
        });
      }
    }

    // 3. Утечка SQL соединений без try-with-resources
    if (/\b(?:createStatement|prepareStatement)\b/.test(line) && !line.includes("try") && !line.includes("(")) {
      // Проверяем 3 строки назад
      const prevContext = lines.slice(Math.max(0, i - 3), i).join(" ");
      if (!prevContext.includes("try (")) {
        issues.push({
          line: lineNum,
          severity: "warning",
          ruleId: "SQL_RESOURCE_LEAK",
          title: "Потенциальная утечка SQL-соединения или Statement",
          message: "Создание Statement/PreparedStatement без блока try-with-resources может исчерпать пул подключений к БД при ошибке.",
          suggestion: "Используйте конструкцию try (PreparedStatement ps = conn.prepareStatement(...)) { ... }.",
        });
      }
    }

    // 4. Метод @EventHandler в классе без implements Listener
    if (/@EventHandler\b/.test(line) && !hasListenerInterface) {
      issues.push({
        line: lineNum,
        severity: "warning",
        ruleId: "UNREGISTERED_LISTENER",
        title: "@EventHandler в классе без Listener",
        message: "Метод помечен аннотацией @EventHandler, но класс не реализует интерфейс org.bukkit.event.Listener.",
        suggestion: "Добавьте implements Listener к объявлению класса, иначе Bukkit проигнорирует события.",
      });
    }

    // 5. Использование player в отложенной задаче без проверки isOnline()
    if (/runTaskLater\b/.test(line)) {
      const nextContext = lines.slice(i, Math.min(lines.length, i + 8)).join(" ");
      if (nextContext.includes("player.") && !nextContext.includes("isOnline()")) {
        issues.push({
          line: lineNum,
          severity: "info",
          ruleId: "MISSING_ONLINE_CHECK",
          title: "Отложенное действие над игроком без проверки онлайн-статуса",
          message: "Игрок может выйти с сервера до того, как сработает таймер runTaskLater.",
          suggestion: "Добавьте проверку if (!player.isOnline()) return; в начале тела задачи.",
        });
      }
    }
  }

  return issues;
}
