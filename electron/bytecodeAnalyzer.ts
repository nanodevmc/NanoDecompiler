// electron/bytecodeAnalyzer.ts - Полный статический анализ байткода Java (.class)
// для плагинов Bukkit/Spigot/Paper.
// Извлекает реальные классы, методы, опкоды байткода, слушатели эвентов (@EventHandler),
// программные команды (getCommand), проверки прав (hasPermission),
// а также автоматически расшифровывает обфусцированные строки (Aegis VM, Allatori, Base64, XOR).

export interface BytecodeInstruction {
  pc: number;
  opcode: number;
  mnemonic: string;
  operand?: string | number;
  comment?: string;
}

export interface BytecodeMethod {
  name: string;
  descriptor: string;
  accessFlags: number;
  isStatic: boolean;
  isPublic: boolean;
  annotations: string[];
  instructions: BytecodeInstruction[];
  insnSummary?: string;
}

export interface BytecodeField {
  name: string;
  descriptor: string;
  accessFlags: number;
  isStatic: boolean;
}

export interface BytecodeClass {
  name: string;
  superName: string;
  interfaces: string[];
  fields: BytecodeField[];
  methods: BytecodeMethod[];
  annotations: string[];
  isListener: boolean;
  isCommandExecutor: boolean;
  rawStringConstants: string[];
}

export interface BytecodeEvent {
  eventClass: string;
  methodName: string;
  listenerClass: string;
  priority?: string;
  ignoreCancelled?: boolean;
}

export interface BytecodeCommand {
  command: string;
  sourceClass: string;
  method: string;
}

export interface BytecodePermission {
  permission: string;
  sourceClass: string;
  method: string;
}

export interface DecryptedStringInfo {
  original: string;
  decrypted: string;
  sourceClass: string;
  method?: string;
  algorithm: string;
}

export interface SecurityVulnerability {
  severity: "CRITICAL" | "HIGH" | "MEDIUM" | "LOW";
  category: "SQL_INJECTION" | "COMMAND_INJECTION" | "RCE" | "ARBITRARY_FILE";
  title: string;
  description: string;
  className: string;
  methodName: string;
  pc: number;
}

export interface DrmCheckFinding {
  hostOrEndpoint?: string;
  methodName: string;
  className: string;
  confidence: "HIGH" | "MEDIUM";
  details: string;
  suggestedPatch: {
    description: string;
    patchHex: string;
  };
}

export interface MemoryLeakRisk {
  type: "STATIC_PLAYER_COLLECTION" | "UNREGISTERED_TASK" | "DANGLING_LISTENER";
  severity: "HIGH" | "MEDIUM";
  className: string;
  fieldName?: string;
  methodName?: string;
  explanation: string;
}

export interface XRefItem {
  target: string;
  callers: Array<{ className: string; methodName: string }>;
  callees: Array<{ className: string; methodName: string }>;
}

export interface SemanticRenameSuggestion {
  originalName: string;
  suggestedName: string;
  reason: string;
  confidence: number;
}

export interface BytecodeAnalysisResult {
  classes: BytecodeClass[];
  events: BytecodeEvent[];
  bytecodeCommands: BytecodeCommand[];
  bytecodePermissions: BytecodePermission[];
  decryptedStrings: DecryptedStringInfo[];
  vulnerabilities: SecurityVulnerability[];
  drmChecks: DrmCheckFinding[];
  memoryLeaks: MemoryLeakRisk[];
  xrefs: Record<string, XRefItem>;
  renameSuggestions: SemanticRenameSuggestion[];
  opaqueTrapsCount: number;
  stats: {
    totalClasses: number;
    totalMethods: number;
    totalInstructions: number;
    totalStrings: number;
    decryptedStringsCount: number;
    vulnerabilitiesCount: number;
    drmChecksCount: number;
    memoryLeaksCount: number;
  };
}

// Java Opcodes Table
const OPCODES: Record<number, { mnemonic: string; operands: number }> = {
  0x00: { mnemonic: "nop", operands: 0 },
  0x01: { mnemonic: "aconst_null", operands: 0 },
  0x02: { mnemonic: "iconst_m1", operands: 0 },
  0x03: { mnemonic: "iconst_0", operands: 0 },
  0x04: { mnemonic: "iconst_1", operands: 0 },
  0x05: { mnemonic: "iconst_2", operands: 0 },
  0x06: { mnemonic: "iconst_3", operands: 0 },
  0x07: { mnemonic: "iconst_4", operands: 0 },
  0x08: { mnemonic: "iconst_5", operands: 0 },
  0x09: { mnemonic: "lconst_0", operands: 0 },
  0x0a: { mnemonic: "lconst_1", operands: 0 },
  0x0b: { mnemonic: "fconst_0", operands: 0 },
  0x0c: { mnemonic: "fconst_1", operands: 0 },
  0x0d: { mnemonic: "fconst_2", operands: 0 },
  0x0e: { mnemonic: "dconst_0", operands: 0 },
  0x0f: { mnemonic: "dconst_1", operands: 0 },
  0x10: { mnemonic: "bipush", operands: 1 },
  0x11: { mnemonic: "sipush", operands: 2 },
  0x12: { mnemonic: "ldc", operands: 1 },
  0x13: { mnemonic: "ldc_w", operands: 2 },
  0x14: { mnemonic: "ldc2_w", operands: 2 },
  0x15: { mnemonic: "iload", operands: 1 },
  0x16: { mnemonic: "lload", operands: 1 },
  0x17: { mnemonic: "fload", operands: 1 },
  0x18: { mnemonic: "dload", operands: 1 },
  0x19: { mnemonic: "aload", operands: 1 },
  0x1a: { mnemonic: "iload_0", operands: 0 },
  0x1b: { mnemonic: "iload_1", operands: 0 },
  0x1c: { mnemonic: "iload_2", operands: 0 },
  0x1d: { mnemonic: "iload_3", operands: 0 },
  0x1e: { mnemonic: "lload_0", operands: 0 },
  0x1f: { mnemonic: "lload_1", operands: 0 },
  0x20: { mnemonic: "lload_2", operands: 0 },
  0x21: { mnemonic: "lload_3", operands: 0 },
  0x22: { mnemonic: "fload_0", operands: 0 },
  0x23: { mnemonic: "fload_1", operands: 0 },
  0x24: { mnemonic: "fload_2", operands: 0 },
  0x25: { mnemonic: "fload_3", operands: 0 },
  0x26: { mnemonic: "dload_0", operands: 0 },
  0x27: { mnemonic: "dload_1", operands: 0 },
  0x28: { mnemonic: "dload_2", operands: 0 },
  0x29: { mnemonic: "dload_3", operands: 0 },
  0x2a: { mnemonic: "aload_0", operands: 0 },
  0x2b: { mnemonic: "aload_1", operands: 0 },
  0x2c: { mnemonic: "aload_2", operands: 0 },
  0x2d: { mnemonic: "aload_3", operands: 0 },
  0x2e: { mnemonic: "iaload", operands: 0 },
  0x2f: { mnemonic: "laload", operands: 0 },
  0x30: { mnemonic: "faload", operands: 0 },
  0x31: { mnemonic: "daload", operands: 0 },
  0x32: { mnemonic: "aaload", operands: 0 },
  0x33: { mnemonic: "baload", operands: 0 },
  0x34: { mnemonic: "caload", operands: 0 },
  0x35: { mnemonic: "saload", operands: 0 },
  0x36: { mnemonic: "istore", operands: 1 },
  0x37: { mnemonic: "lstore", operands: 1 },
  0x38: { mnemonic: "fstore", operands: 1 },
  0x39: { mnemonic: "dstore", operands: 1 },
  0x3a: { mnemonic: "astore", operands: 1 },
  0x3b: { mnemonic: "istore_0", operands: 0 },
  0x3c: { mnemonic: "istore_1", operands: 0 },
  0x3d: { mnemonic: "istore_2", operands: 0 },
  0x3e: { mnemonic: "istore_3", operands: 0 },
  0x3f: { mnemonic: "lstore_0", operands: 0 },
  0x40: { mnemonic: "lstore_1", operands: 0 },
  0x41: { mnemonic: "lstore_2", operands: 0 },
  0x42: { mnemonic: "lstore_3", operands: 0 },
  0x43: { mnemonic: "fstore_0", operands: 0 },
  0x44: { mnemonic: "fstore_1", operands: 0 },
  0x45: { mnemonic: "fstore_2", operands: 0 },
  0x46: { mnemonic: "fstore_3", operands: 0 },
  0x47: { mnemonic: "dstore_0", operands: 0 },
  0x48: { mnemonic: "dstore_1", operands: 0 },
  0x49: { mnemonic: "dstore_2", operands: 0 },
  0x4a: { mnemonic: "dstore_3", operands: 0 },
  0x4b: { mnemonic: "astore_0", operands: 0 },
  0x4c: { mnemonic: "astore_1", operands: 0 },
  0x4d: { mnemonic: "astore_2", operands: 0 },
  0x4e: { mnemonic: "astore_3", operands: 0 },
  0x4f: { mnemonic: "iastore", operands: 0 },
  0x50: { mnemonic: "lastore", operands: 0 },
  0x51: { mnemonic: "fastore", operands: 0 },
  0x52: { mnemonic: "dastore", operands: 0 },
  0x53: { mnemonic: "aastore", operands: 0 },
  0x54: { mnemonic: "bastore", operands: 0 },
  0x55: { mnemonic: "castore", operands: 0 },
  0x56: { mnemonic: "sastore", operands: 0 },
  0x57: { mnemonic: "pop", operands: 0 },
  0x58: { mnemonic: "pop2", operands: 0 },
  0x59: { mnemonic: "dup", operands: 0 },
  0x5a: { mnemonic: "dup_x1", operands: 0 },
  0x5b: { mnemonic: "dup_x2", operands: 0 },
  0x5c: { mnemonic: "dup2", operands: 0 },
  0x5d: { mnemonic: "dup2_x1", operands: 0 },
  0x5e: { mnemonic: "dup2_x2", operands: 0 },
  0x5f: { mnemonic: "swap", operands: 0 },
  0x60: { mnemonic: "iadd", operands: 0 },
  0x61: { mnemonic: "ladd", operands: 0 },
  0x62: { mnemonic: "fadd", operands: 0 },
  0x63: { mnemonic: "dadd", operands: 0 },
  0x64: { mnemonic: "isub", operands: 0 },
  0x65: { mnemonic: "lsub", operands: 0 },
  0x66: { mnemonic: "fsub", operands: 0 },
  0x67: { mnemonic: "dsub", operands: 0 },
  0x68: { mnemonic: "imul", operands: 0 },
  0x69: { mnemonic: "lmul", operands: 0 },
  0x6a: { mnemonic: "fmul", operands: 0 },
  0x6b: { mnemonic: "dmul", operands: 0 },
  0x6c: { mnemonic: "idiv", operands: 0 },
  0x6d: { mnemonic: "ldiv", operands: 0 },
  0x6e: { mnemonic: "fdiv", operands: 0 },
  0x6f: { mnemonic: "ddiv", operands: 0 },
  0x70: { mnemonic: "irem", operands: 0 },
  0x71: { mnemonic: "lrem", operands: 0 },
  0x72: { mnemonic: "frem", operands: 0 },
  0x73: { mnemonic: "drem", operands: 0 },
  0x74: { mnemonic: "ineg", operands: 0 },
  0x75: { mnemonic: "lneg", operands: 0 },
  0x76: { mnemonic: "fneg", operands: 0 },
  0x77: { mnemonic: "dneg", operands: 0 },
  0x78: { mnemonic: "ishl", operands: 0 },
  0x79: { mnemonic: "lshl", operands: 0 },
  0x7a: { mnemonic: "ishr", operands: 0 },
  0x7b: { mnemonic: "lshr", operands: 0 },
  0x7c: { mnemonic: "iushr", operands: 0 },
  0x7d: { mnemonic: "lushr", operands: 0 },
  0x7e: { mnemonic: "iand", operands: 0 },
  0x7f: { mnemonic: "land", operands: 0 },
  0x80: { mnemonic: "ior", operands: 0 },
  0x81: { mnemonic: "lor", operands: 0 },
  0x82: { mnemonic: "ixor", operands: 0 },
  0x83: { mnemonic: "lxor", operands: 0 },
  0x84: { mnemonic: "iinc", operands: 2 },
  0x85: { mnemonic: "i2l", operands: 0 },
  0x86: { mnemonic: "i2f", operands: 0 },
  0x87: { mnemonic: "i2d", operands: 0 },
  0x88: { mnemonic: "l2i", operands: 0 },
  0x89: { mnemonic: "l2f", operands: 0 },
  0x8a: { mnemonic: "l2d", operands: 0 },
  0x8b: { mnemonic: "f2i", operands: 0 },
  0x8c: { mnemonic: "f2l", operands: 0 },
  0x8d: { mnemonic: "f2d", operands: 0 },
  0x8e: { mnemonic: "d2i", operands: 0 },
  0x8f: { mnemonic: "d2l", operands: 0 },
  0x90: { mnemonic: "d2f", operands: 0 },
  0x91: { mnemonic: "i2b", operands: 0 },
  0x92: { mnemonic: "i2c", operands: 0 },
  0x93: { mnemonic: "i2s", operands: 0 },
  0x94: { mnemonic: "lcmp", operands: 0 },
  0x95: { mnemonic: "fcmpl", operands: 0 },
  0x96: { mnemonic: "fcmpg", operands: 0 },
  0x97: { mnemonic: "dcmpl", operands: 0 },
  0x98: { mnemonic: "dcmpg", operands: 0 },
  0x99: { mnemonic: "ifeq", operands: 2 },
  0x9a: { mnemonic: "ifne", operands: 2 },
  0x9b: { mnemonic: "iflt", operands: 2 },
  0x9c: { mnemonic: "ifge", operands: 2 },
  0x9d: { mnemonic: "ifgt", operands: 2 },
  0x9e: { mnemonic: "ifle", operands: 2 },
  0x9f: { mnemonic: "if_icmpeq", operands: 2 },
  0xa0: { mnemonic: "if_icmpne", operands: 2 },
  0xa1: { mnemonic: "if_icmplt", operands: 2 },
  0xa2: { mnemonic: "if_icmpge", operands: 2 },
  0xa3: { mnemonic: "if_icmpgt", operands: 2 },
  0xa4: { mnemonic: "if_icmple", operands: 2 },
  0xa5: { mnemonic: "if_acmpeq", operands: 2 },
  0xa6: { mnemonic: "if_acmpne", operands: 2 },
  0xa7: { mnemonic: "goto", operands: 2 },
  0xa8: { mnemonic: "jsr", operands: 2 },
  0xa9: { mnemonic: "ret", operands: 1 },
  0xaa: { mnemonic: "tableswitch", operands: -1 },
  0xab: { mnemonic: "lookupswitch", operands: -1 },
  0xac: { mnemonic: "ireturn", operands: 0 },
  0xad: { mnemonic: "lreturn", operands: 0 },
  0xae: { mnemonic: "freturn", operands: 0 },
  0xaf: { mnemonic: "dreturn", operands: 0 },
  0xb0: { mnemonic: "areturn", operands: 0 },
  0xb1: { mnemonic: "return", operands: 0 },
  0xb2: { mnemonic: "getstatic", operands: 2 },
  0xb3: { mnemonic: "putstatic", operands: 2 },
  0xb4: { mnemonic: "getfield", operands: 2 },
  0xb5: { mnemonic: "putfield", operands: 2 },
  0xb6: { mnemonic: "invokevirtual", operands: 2 },
  0xb7: { mnemonic: "invokespecial", operands: 2 },
  0xb8: { mnemonic: "invokestatic", operands: 2 },
  0xb9: { mnemonic: "invokeinterface", operands: 4 },
  0xba: { mnemonic: "invokedynamic", operands: 4 },
  0xbb: { mnemonic: "new", operands: 2 },
  0xbc: { mnemonic: "newarray", operands: 1 },
  0xbd: { mnemonic: "anewarray", operands: 2 },
  0xbe: { mnemonic: "arraylength", operands: 0 },
  0xbf: { mnemonic: "athrow", operands: 0 },
  0xc0: { mnemonic: "checkcast", operands: 2 },
  0xc1: { mnemonic: "instanceof", operands: 2 },
  0xc2: { mnemonic: "monitorenter", operands: 0 },
  0xc3: { mnemonic: "monitorexit", operands: 0 },
  0xc4: { mnemonic: "wide", operands: -1 },
  0xc5: { mnemonic: "multianewarray", operands: 3 },
  0xc6: { mnemonic: "ifnull", operands: 2 },
  0xc7: { mnemonic: "ifnonnull", operands: 2 },
  0xc8: { mnemonic: "goto_w", operands: 4 },
  0xc9: { mnemonic: "jsr_w", operands: 4 },
};

/** Читатель бинарного формата Java .class */
class ClassReader {
  private offset = 0;
  constructor(private readonly buf: Buffer) {}

  readU1(): number {
    const val = this.buf.readUInt8(this.offset);
    this.offset += 1;
    return val;
  }

  readU2(): number {
    const val = this.buf.readUInt16BE(this.offset);
    this.offset += 2;
    return val;
  }

  readU4(): number {
    const val = this.buf.readUInt32BE(this.offset);
    this.offset += 4;
    return val;
  }

  skip(bytes: number) {
    this.offset += bytes;
  }

  getOffset(): number {
    return this.offset;
  }

  setOffset(pos: number) {
    this.offset = pos;
  }

  hasRemaining(bytes = 1): boolean {
    return this.offset + bytes <= this.buf.length;
  }

  readBytes(len: number): Buffer {
    const b = this.buf.slice(this.offset, this.offset + len);
    this.offset += len;
    return b;
  }
}

/** Вспомогательный класс для разбора Constant Pool */
class ConstantPool {
  private entries: any[] = [];

  constructor(private readonly size: number) {
    this.entries = new Array(size);
  }

  set(idx: number, entry: any) {
    this.entries[idx] = entry;
  }

  get(idx: number): any {
    return this.entries[idx];
  }

  getUtf8(idx: number): string | undefined {
    const e = this.entries[idx];
    return e && e.tag === 1 ? e.value : undefined;
  }

  getClassName(idx: number): string | undefined {
    const e = this.entries[idx];
    if (e && e.tag === 7) {
      return this.getUtf8(e.nameIndex);
    }
    return undefined;
  }

  getString(idx: number): string | undefined {
    const e = this.entries[idx];
    if (e && e.tag === 8) {
      return this.getUtf8(e.stringIndex);
    }
    return undefined;
  }

  getMethodRef(idx: number): { className: string; methodName: string; descriptor: string } | undefined {
    const e = this.entries[idx];
    if (e && (e.tag === 10 || e.tag === 11)) {
      const cls = this.getClassName(e.classIndex) || "";
      const nt = this.entries[e.nameAndTypeIndex];
      if (nt && nt.tag === 12) {
        const name = this.getUtf8(nt.nameIndex) || "";
        const desc = this.getUtf8(nt.descIndex) || "";
        return { className: cls, methodName: name, descriptor: desc };
      }
    }
    return undefined;
  }

  getFieldRef(idx: number): { className: string; fieldName: string; descriptor: string } | undefined {
    const e = this.entries[idx];
    if (e && e.tag === 9) {
      const cls = this.getClassName(e.classIndex) || "";
      const nt = this.entries[e.nameAndTypeIndex];
      if (nt && nt.tag === 12) {
        const name = this.getUtf8(nt.nameIndex) || "";
        const desc = this.getUtf8(nt.descIndex) || "";
        return { className: cls, fieldName: name, descriptor: desc };
      }
    }
    return undefined;
  }
}

/** Разбирает единичный файл .class */
export function parseClassFile(buf: Buffer): BytecodeClass | null {
  if (buf.length < 10) return null;
  const r = new ClassReader(buf);

  // Magic
  const magic = r.readU4();
  if (magic !== 0xcafebabe) return null;

  // Versions
  r.readU2(); // minor
  r.readU2(); // major

  // Constant Pool
  const cpCount = r.readU2();
  const cp = new ConstantPool(cpCount);
  const rawStringConstants: string[] = [];

  for (let i = 1; i < cpCount; i++) {
    const tag = r.readU1();
    switch (tag) {
      case 1: { // Utf8
        const len = r.readU2();
        const strBytes = r.readBytes(len);
        const val = strBytes.toString("utf-8");
        cp.set(i, { tag, value: val });
        break;
      }
      case 3: // Integer
        r.readU4();
        cp.set(i, { tag });
        break;
      case 4: // Float
        r.readU4();
        cp.set(i, { tag });
        break;
      case 5: // Long
      case 6: // Double
        r.readU4();
        r.readU4();
        cp.set(i, { tag });
        i++; // Takes two slots
        break;
      case 7: { // Class
        const nameIndex = r.readU2();
        cp.set(i, { tag, nameIndex });
        break;
      }
      case 8: { // String
        const stringIndex = r.readU2();
        cp.set(i, { tag, stringIndex });
        break;
      }
      case 9: // Fieldref
      case 10: // Methodref
      case 11: { // InterfaceMethodref
        const classIndex = r.readU2();
        const nameAndTypeIndex = r.readU2();
        cp.set(i, { tag, classIndex, nameAndTypeIndex });
        break;
      }
      case 12: { // NameAndType
        const nameIndex = r.readU2();
        const descIndex = r.readU2();
        cp.set(i, { tag, nameIndex, descIndex });
        break;
      }
      case 15: // MethodHandle
        r.readU1();
        r.readU2();
        cp.set(i, { tag });
        break;
      case 16: // MethodType
        r.readU2();
        cp.set(i, { tag });
        break;
      case 17: // Dynamic
      case 18: // InvokeDynamic
        r.readU2();
        r.readU2();
        cp.set(i, { tag });
        break;
      case 19: // Module
      case 20: // Package
        r.readU2();
        cp.set(i, { tag });
        break;
      default:
        // Неизвестный тег - останавливаем разбор
        return null;
    }
  }

  // Собираем все явные String константы из пула
  for (let i = 1; i < cpCount; i++) {
    const s = cp.getString(i);
    if (s !== undefined && s.length > 0) {
      rawStringConstants.push(s);
    }
  }

  // Access flags, this_class, super_class
  r.readU2();
  const thisClassIdx = r.readU2();
  const superClassIdx = r.readU2();

  const className = (cp.getClassName(thisClassIdx) || "").replace(/\//g, ".");
  const superName = (cp.getClassName(superClassIdx) || "").replace(/\//g, ".");

  // Interfaces
  const interfacesCount = r.readU2();
  const interfaces: string[] = [];
  for (let i = 0; i < interfacesCount; i++) {
    const ifaceIdx = r.readU2();
    const ifaceName = (cp.getClassName(ifaceIdx) || "").replace(/\//g, ".");
    if (ifaceName) interfaces.push(ifaceName);
  }

  const isListener = interfaces.includes("org.bukkit.event.Listener") || superName.includes("Listener");
  const isCommandExecutor =
    interfaces.includes("org.bukkit.command.CommandExecutor") ||
    interfaces.includes("org.bukkit.command.TabExecutor") ||
    interfaces.includes("org.bukkit.command.TabCompleter");

  // Fields
  const fieldsCount = r.readU2();
  const fields: BytecodeField[] = [];
  for (let i = 0; i < fieldsCount; i++) {
    const access = r.readU2();
    const nameIdx = r.readU2();
    const descIdx = r.readU2();
    const name = cp.getUtf8(nameIdx) || "";
    const desc = cp.getUtf8(descIdx) || "";
    const attrCount = r.readU2();
    for (let a = 0; a < attrCount; a++) {
      r.readU2();
      const attrLen = r.readU4();
      r.skip(attrLen);
    }
    fields.push({
      name,
      descriptor: desc,
      accessFlags: access,
      isStatic: (access & 0x0008) !== 0,
    });
  }

  // Methods
  const methodsCount = r.readU2();
  const methods: BytecodeMethod[] = [];

  for (let i = 0; i < methodsCount; i++) {
    const access = r.readU2();
    const nameIdx = r.readU2();
    const descIdx = r.readU2();
    const name = cp.getUtf8(nameIdx) || "";
    const desc = cp.getUtf8(descIdx) || "";
    const attrCount = r.readU2();

    const annotations: string[] = [];
    const instructions: BytecodeInstruction[] = [];

    for (let a = 0; a < attrCount; a++) {
      const attrNameIdx = r.readU2();
      const attrLen = r.readU4();
      const attrName = cp.getUtf8(attrNameIdx) || "";

      if (attrName === "Code") {
        const codeStart = r.getOffset();
        r.readU2(); // max_stack
        r.readU2(); // max_locals
        const codeLen = r.readU4();
        const codeBytesEnd = r.getOffset() + codeLen;

        // Разбор опкодов метода
        while (r.getOffset() < codeBytesEnd) {
          const pc = r.getOffset() - (codeStart + 8);
          const op = r.readU1();
          const info = OPCODES[op] || { mnemonic: `0x${op.toString(16)}`, operands: 0 };
          let operandStr: string | number | undefined;
          let comment: string | undefined;

          if (info.mnemonic === "ldc") {
            const cIdx = r.readU1();
            const strVal = cp.getString(cIdx) || cp.getUtf8(cIdx);
            operandStr = `#${cIdx}`;
            if (strVal !== undefined) comment = `"${strVal}"`;
          } else if (info.mnemonic === "ldc_w" || info.mnemonic === "ldc2_w") {
            const cIdx = r.readU2();
            const strVal = cp.getString(cIdx) || cp.getUtf8(cIdx);
            operandStr = `#${cIdx}`;
            if (strVal !== undefined) comment = `"${strVal}"`;
          } else if (info.mnemonic.startsWith("invoke")) {
            const mIdx = r.readU2();
            if (info.mnemonic === "invokeinterface" || info.mnemonic === "invokedynamic") {
              r.readU1();
              r.readU1();
            }
            const mRef = cp.getMethodRef(mIdx);
            operandStr = `#${mIdx}`;
            if (mRef) {
              comment = `${mRef.className.replace(/\//g, ".")}.${mRef.methodName}${mRef.descriptor}`;
            }
          } else if (info.mnemonic.startsWith("get") || info.mnemonic.startsWith("put")) {
            const fIdx = r.readU2();
            const fRef = cp.getFieldRef(fIdx);
            operandStr = `#${fIdx}`;
            if (fRef) {
              comment = `${fRef.className.replace(/\//g, ".")}.${fRef.fieldName}`;
            }
          } else if (info.operands > 0) {
            if (info.operands === 1) operandStr = r.readU1();
            else if (info.operands === 2) operandStr = r.readU2();
            else if (info.operands === 4) operandStr = r.readU4();
            else r.skip(info.operands);
          } else if (info.operands === -1) {
            // tableswitch / lookupswitch / wide - просто скипаем до конца байткода или выравнивания
            const pad = (4 - ((pc + 1) % 4)) % 4;
            r.skip(pad);
            // Default offset
            r.readU4();
            if (info.mnemonic === "tableswitch") {
              const low = r.readU4();
              const high = r.readU4();
              const count = high - low + 1;
              if (count > 0 && count < 1000) r.skip(count * 4);
            } else if (info.mnemonic === "lookupswitch") {
              const count = r.readU4();
              if (count > 0 && count < 1000) r.skip(count * 8);
            }
          }

          instructions.push({
            pc,
            opcode: op,
            mnemonic: info.mnemonic,
            operand: operandStr,
            comment,
          });
        }

        // Skip exception table & sub-attributes
        r.setOffset(codeStart + attrLen);
      } else if (attrName.includes("Annotation")) {
        const rawAnnot = r.readBytes(attrLen).toString("latin1");
        if (rawAnnot.includes("EventHandler")) annotations.push("@EventHandler");
        if (rawAnnot.includes("Override")) annotations.push("@Override");
        if (rawAnnot.includes("Deprecated")) annotations.push("@Deprecated");
      } else {
        r.skip(attrLen);
      }
    }

    methods.push({
      name,
      descriptor: desc,
      accessFlags: access,
      isStatic: (access & 0x0008) !== 0,
      isPublic: (access & 0x0001) !== 0,
      annotations,
      instructions,
      insnSummary: `${instructions.length} инстр. (${instructions.slice(0, 5).map((x) => x.mnemonic).join(", ")}...)`,
    });
  }

  // Class level attributes
  const classAttrCount = r.readU2();
  const classAnnotations: string[] = [];
  for (let a = 0; a < classAttrCount; a++) {
    const attrNameIdx = r.readU2();
    const attrLen = r.readU4();
    const attrName = cp.getUtf8(attrNameIdx) || "";
    if (attrName.includes("Annotation")) {
      const raw = r.readBytes(attrLen).toString("latin1");
      if (raw.includes("Deprecated")) classAnnotations.push("@Deprecated");
    } else {
      r.skip(attrLen);
    }
  }

  return {
    name: className,
    superName,
    interfaces,
    fields,
    methods,
    annotations: classAnnotations,
    isListener,
    isCommandExecutor,
    rawStringConstants,
  };
}

// ---------------------------------------------------------------------------
// Эвристический деобфускатор и расшифровщик строк
// ---------------------------------------------------------------------------

/** Проверка, читаема ли строка как нормальный текст */
function isHumanReadable(str: string): boolean {
  if (!str || str.length < 2) return false;
  let printable = 0;
  let letters = 0;
  for (let i = 0; i < str.length; i++) {
    const code = str.charCodeAt(i);
    // ASCII печатные
    if (code >= 32 && code <= 126) {
      printable++;
      if ((code >= 65 && code <= 90) || (code >= 97 && code <= 122)) letters++;
    } else if ((code >= 1040 && code <= 1103) || code === 1025 || code === 1105) {
      // Кириллица
      printable++;
      letters++;
    } else if (code === 9 || code === 10 || code === 13) {
      printable++;
    }
  }
  // Должно быть высокое число печатных символов И хотя бы несколько реальных букв
  return printable / str.length >= 0.88 && letters >= Math.min(3, str.length);
}

/** Расшифровка строк Base64 */
function tryDecryptBase64(str: string): string | null {
  const trimmed = str.trim();
  if (trimmed.length < 8 || trimmed.length % 4 !== 0) return null;
  if (!/^[A-Za-z0-9+/]+={0,2}$/.test(trimmed)) return null;

  try {
    const buf = Buffer.from(trimmed, "base64");
    if (buf.length < 4) return null;
    const decoded = buf.toString("utf-8");
    if (isHumanReadable(decoded) && decoded !== trimmed) {
      return decoded;
    }
  } catch {
    // ignore
  }
  return null;
}

/** Расшифровка строк Aegis VM / Allatori (посимвольный XOR) */
function tryDecryptXor(str: string): { decrypted: string; key: number; algorithm: string } | null {
  if (!str || str.length < 3) return null;

  // Если строка УЖЕ читаема (например, русский текст в UTF-8 или конфиг) - она НЕ зашифрована
  if (isHumanReadable(str)) return null;

  // Перебор вероятных 1-байтовых ключей XOR (1..255)
  for (let k = 1; k < 256; k++) {
    let cand = "";
    for (let i = 0; i < str.length; i++) {
      cand += String.fromCharCode(str.charCodeAt(i) ^ k);
    }
    if (isHumanReadable(cand)) {
      return { decrypted: cand, key: k, algorithm: `Aegis XOR (key: 0x${k.toString(16)})` };
    }
  }

  // Allatori 2-step XOR (key ^ (i & 0x1F))
  for (let k = 1; k < 256; k++) {
    let cand = "";
    for (let i = 0; i < str.length; i++) {
      cand += String.fromCharCode(str.charCodeAt(i) ^ (k ^ (i & 0x1f)));
    }
    if (isHumanReadable(cand)) {
      return { decrypted: cand, key: k, algorithm: `Allatori Dynamic XOR (seed: 0x${k.toString(16)})` };
    }
  }

  return null;
}

/**
 * Проводит полный анализ набора байткод-классов:
 * - Вытаскивает слушатели и события
 * - Находит команды и пермишены в байткоде
 * - Расшифровывает обфусцированные строки
 */
export function analyzeBytecodeClasses(classes: BytecodeClass[]): BytecodeAnalysisResult {
  const events: BytecodeEvent[] = [];
  const bytecodeCommands: BytecodeCommand[] = [];
  const bytecodePermissions: BytecodePermission[] = [];
  const decryptedStrings: DecryptedStringInfo[] = [];
  const vulnerabilities: SecurityVulnerability[] = [];
  const drmChecks: DrmCheckFinding[] = [];
  const memoryLeaks: MemoryLeakRisk[] = [];
  const xrefs: Record<string, XRefItem> = {};
  const renameSuggestions: SemanticRenameSuggestion[] = [];
  let opaqueTrapsCount = 0;

  let totalMethods = 0;
  let totalInstructions = 0;
  let totalStrings = 0;

  for (const cls of classes) {
    totalStrings += cls.rawStringConstants.length;

    // 1. Поиск расшифровываемых строк в пуле констант
    for (const rawStr of cls.rawStringConstants) {
      const b64 = tryDecryptBase64(rawStr);
      if (b64) {
        decryptedStrings.push({
          original: rawStr,
          decrypted: b64,
          sourceClass: cls.name,
          algorithm: "Base64 Obfuscation",
        });
        continue;
      }

      const xor = tryDecryptXor(rawStr);
      if (xor) {
        decryptedStrings.push({
          original: rawStr,
          decrypted: xor.decrypted,
          sourceClass: cls.name,
          algorithm: xor.algorithm,
        });
      }
    }

    // 2. Анализ полей на утечки памяти (static коллекции игроков)
    for (const f of cls.fields) {
      if (f.isStatic && (f.descriptor.includes("Player") || f.descriptor.includes("org/bukkit/entity/Player"))) {
        memoryLeaks.push({
          type: "STATIC_PLAYER_COLLECTION",
          severity: "HIGH",
          className: cls.name,
          fieldName: f.name,
          explanation: `Статическое поле '${f.name}' содержит ссылку на org.bukkit.entity.Player (${f.descriptor}). При выходе игрока мир и профиль не будут освобождены GC.`,
        });
      }
    }

    // 3. Семантическое предложение имени (для обфусцированных классов типа a, b, c)
    const simpleClsName = cls.name.split(".").pop() || "";
    if (simpleClsName.length <= 2) {
      if (cls.isCommandExecutor) {
        renameSuggestions.push({
          originalName: cls.name,
          suggestedName: `${simpleClsName.toUpperCase()}CommandManager`,
          reason: "Класс реализует интерфейс CommandExecutor / TabCompleter",
          confidence: 0.95,
        });
      } else if (cls.isListener) {
        renameSuggestions.push({
          originalName: cls.name,
          suggestedName: `${simpleClsName.toUpperCase()}EventListener`,
          reason: "Класс реализует org.bukkit.event.Listener",
          confidence: 0.92,
        });
      } else if (cls.rawStringConstants.some((s) => s.toLowerCase().includes("select") && s.toLowerCase().includes("from"))) {
        renameSuggestions.push({
          originalName: cls.name,
          suggestedName: `${simpleClsName.toUpperCase()}DatabaseHandler`,
          reason: "Содержит константы SQL запросов",
          confidence: 0.88,
        });
      }
    }

    // 4. Анализ методов
    for (const m of cls.methods) {
      totalMethods++;
      totalInstructions += m.instructions.length;

      const currentMethodKey = `${cls.name}.${m.name}()`;
      if (!xrefs[currentMethodKey]) {
        xrefs[currentMethodKey] = { target: currentMethodKey, callers: [], callees: [] };
      }

      // Слушатели эвентов Bukkit
      const isEventHandler = m.annotations.includes("@EventHandler") || cls.isListener;
      if (isEventHandler && m.descriptor.startsWith("(L") && m.descriptor.includes("Event;")) {
        const match = m.descriptor.match(/\(L([^;]+Event);/);
        if (match) {
          const eventClass = match[1].replace(/\//g, ".");
          events.push({
            eventClass,
            methodName: m.name,
            listenerClass: cls.name,
          });
        }
      }

      // Анализ опкодов
      for (let i = 0; i < m.instructions.length; i++) {
        const insn = m.instructions[i];

        // Детектор ложных предикатов (Opaque Predicates: iconst_0; ifne или iconst_1; ifeq)
        if (i > 0) {
          const prev = m.instructions[i - 1];
          if ((prev.mnemonic === "iconst_0" && insn.mnemonic === "ifne") ||
              (prev.mnemonic === "iconst_1" && insn.mnemonic === "ifeq")) {
            opaqueTrapsCount++;
          }
        }

        // Вызовы методов (invokevirtual, invokestatic, etc.)
        if (insn.comment && insn.mnemonic.startsWith("invoke")) {
          const calledTarget = insn.comment;
          xrefs[currentMethodKey].callees.push({ className: cls.name, methodName: calledTarget });

          // Регистрируем входящий вызов (caller)
          if (!xrefs[calledTarget]) {
            xrefs[calledTarget] = { target: calledTarget, callers: [], callees: [] };
          }
          xrefs[calledTarget].callers.push({ className: cls.name, methodName: m.name });

          // 2. Уязвимость: SQL Injection
          if (calledTarget.includes("Statement.execute") || calledTarget.includes("Statement.executeQuery") || calledTarget.includes("Statement.executeUpdate")) {
            // Проверяем, была ли перед этим конкатенация строк StringBuilder
            const hasConcatNearby = m.instructions.slice(Math.max(0, i - 10), i).some((x) => x.comment && x.comment.includes("StringBuilder.append"));
            if (hasConcatNearby) {
              vulnerabilities.push({
                severity: "CRITICAL",
                category: "SQL_INJECTION",
                title: "Потенциальная SQL-инъекция через Statement",
                description: `Вызов ${calledTarget} с динамической конкатенацией параметров через StringBuilder без PreparedStatement.`,
                className: cls.name,
                methodName: m.name,
                pc: insn.pc,
              });
            }
          }

          // 2. Уязвимость: Command Injection
          if (calledTarget.includes("dispatchCommand") || calledTarget.includes("Runtime.exec")) {
            vulnerabilities.push({
              severity: "HIGH",
              category: "COMMAND_INJECTION",
              title: "Выполнение серверной команды",
              description: `Прямой вызов ${calledTarget}. Требуется строгая валидация входящих аргументов и прав игрока.`,
              className: cls.name,
              methodName: m.name,
              pc: insn.pc,
            });
          }

          // 3. DRM & License Check Sniffer
          const isNetCall = calledTarget.includes("HttpURLConnection") || calledTarget.includes("HttpClient") || calledTarget.includes("URL.openConnection");
          if (isNetCall) {
            const nearbyStrings = cls.rawStringConstants.filter((s) => /http|api|license|verify|spigot|auth/i.test(s));
            drmChecks.push({
              hostOrEndpoint: nearbyStrings[0] || "Remote Web API",
              className: cls.name,
              methodName: m.name,
              confidence: nearbyStrings.length > 0 ? "HIGH" : "MEDIUM",
              details: `Обнаружен сетевой запрос проверки в ${calledTarget}. Возможная привязка к DRM/лицензии.`,
              suggestedPatch: {
                description: `Патч No-DRM: замена проверки в ${m.name}() на безусловный return true (iconst_1; ireturn)`,
                patchHex: `04 ac (iconst_1; ireturn) @ offset ${insn.pc}`,
              },
            });
          }
        }

        // Поиск вызовов getCommand("имя")
        if (insn.comment && insn.comment.includes("getCommand(Ljava/lang/String;)")) {
          const prev = m.instructions[i - 1];
          if (prev && prev.comment && prev.comment.startsWith('"')) {
            const cmdName = prev.comment.replace(/^"|"$/g, "").trim();
            if (cmdName && !bytecodeCommands.some((c) => c.command === `/${cmdName}`)) {
              bytecodeCommands.push({
                command: `/${cmdName}`,
                sourceClass: cls.name,
                method: m.name,
              });
            }
          }
        }

        // Поиск вызовов hasPermission("право")
        if (insn.comment && insn.comment.includes("hasPermission(Ljava/lang/String;)")) {
          const prev = m.instructions[i - 1];
          if (prev && prev.comment && prev.comment.startsWith('"')) {
            const perm = prev.comment.replace(/^"|"$/g, "").trim();
            if (perm && !bytecodePermissions.some((p) => p.permission === perm)) {
              bytecodePermissions.push({
                permission: perm,
                sourceClass: cls.name,
                method: m.name,
              });
            }
          }
        }
      }
    }
  }

  return {
    classes,
    events,
    bytecodeCommands,
    bytecodePermissions,
    decryptedStrings,
    vulnerabilities,
    drmChecks,
    memoryLeaks,
    xrefs,
    renameSuggestions,
    opaqueTrapsCount,
    stats: {
      totalClasses: classes.length,
      totalMethods,
      totalInstructions,
      totalStrings,
      decryptedStringsCount: decryptedStrings.length,
      vulnerabilitiesCount: vulnerabilities.length,
      drmChecksCount: drmChecks.length,
      memoryLeaksCount: memoryLeaks.length,
    },
  };
}
