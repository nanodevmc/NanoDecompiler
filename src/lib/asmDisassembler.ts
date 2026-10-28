// src/lib/asmDisassembler.ts - Генератор читаемого листинга байткода ASM
// для Split View и детального анализа опкодов

export function generateAsmListing(javaCode: string, className = "PluginClass"): string {
  const lines = javaCode.split("\n");
  let asm = `// ============================================================================\n`;
  asm += `// ASM DISASSEMBLER LISTING: ${className}\n`;
  asm += `// ============================================================================\n\n`;

  asm += `// class version 61.0 (Java 17)\n`;
  asm += `// access flags 0x21\n`;
  asm += `public class ${className} {\n\n`;

  let currentMethod = "";
  let insideMethod = false;
  let methodLines: string[] = [];

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const trimmed = line.trim();

    // Поиск объявления метода
    const methodMatch = trimmed.match(/(public|private|protected|static|final|\s)*\s+([\w<>\[\]]+)\s+([a-zA-Z0-9_$]+)\s*\(([^)]*)\)\s*\{?/);
    if (methodMatch && !trimmed.startsWith("//") && !trimmed.startsWith("if") && !trimmed.startsWith("for") && !trimmed.startsWith("while") && !trimmed.startsWith("catch")) {
      const returnType = methodMatch[2];
      const methodName = methodMatch[3];
      const params = methodMatch[4];

      if (methodName !== "class" && methodName !== "interface") {
        currentMethod = methodName;
        insideMethod = true;
        methodLines = [];
        asm += `  // access flags 0x1\n`;
        asm += `  public ${methodName}(${params}) : ${returnType}\n`;
        asm += `   L0\n`;
        asm += `    LINENUMBER ${i + 1} L0\n`;
        continue;
      }
    }

    if (insideMethod) {
      if (trimmed === "}") {
        insideMethod = false;
        // Генерируем типовой ASM выход из метода
        asm += `   L1\n`;
        asm += `    LINENUMBER ${i + 1} L1\n`;
        if (currentMethod === "<init>") {
          asm += `    ALOAD 0\n`;
          asm += `    INVOKESPECIAL java/lang/Object.<init> ()V\n`;
          asm += `    RETURN\n`;
        } else if (trimmed.includes("return true") || methodLines.some(l => l.includes("return true"))) {
          asm += `    ICONST_1\n`;
          asm += `    IRETURN\n`;
        } else if (trimmed.includes("return false") || methodLines.some(l => l.includes("return false"))) {
          asm += `    ICONST_0\n`;
          asm += `    IRETURN\n`;
        } else if (trimmed.includes("return null") || methodLines.some(l => l.includes("return null"))) {
          asm += `    ACONST_NULL\n`;
          asm += `    ARETURN\n`;
        } else {
          asm += `    RETURN\n`;
        }
        asm += `    MAXSTACK = 4\n`;
        asm += `    MAXLOCALS = 3\n\n`;
        continue;
      }

      methodLines.push(trimmed);

      // Генерируем опкоды по строкам кода
      if (trimmed.includes("this.")) {
        asm += `    ALOAD 0\n`;
      }
      if (trimmed.includes("Bukkit.get")) {
        asm += `    INVOKESTATIC org/bukkit/Bukkit.getServer ()Lorg/bukkit/Server;\n`;
      }
      if (trimmed.includes("sendMessage(")) {
        asm += `    ALOAD 1\n`;
        asm += `    LDC "MessagePayload"\n`;
        asm += `    INVOKEINTERFACE org/bukkit/command/CommandSender.sendMessage (Ljava/lang/String;)V\n`;
      }
      if (trimmed.includes("hasPermission(")) {
        asm += `    ALOAD 1\n`;
        asm += `    LDC "permission.node"\n`;
        asm += `    INVOKEINTERFACE org/bukkit/permissions/Permissible.hasPermission (Ljava/lang/String;)Z\n`;
        asm += `    IFNE L_PERM_OK\n`;
      }
      if (trimmed.includes("if (") || trimmed.includes("if(")) {
        asm += `    IFEQ L_ELSE_${i}\n`;
      }
      if (trimmed.includes("new ")) {
        asm += `    NEW java/lang/Object\n`;
        asm += `    DUP\n`;
      }
    }
  }

  asm += `}\n`;
  return asm;
}
