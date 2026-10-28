import { useState, useMemo } from "react";
import { BookOpen, Search, ArrowRight, Copy, Check, X, Code2 } from "lucide-react";

interface MappingEntry {
  obf: string;
  mojang: string;
  category: "Class" | "Method" | "Field";
  comment?: string;
}

export function DeobfMappingModal({
  open,
  onClose,
}: {
  open: boolean;
  onClose: () => void;
}) {
  const [search, setSearch] = useState("");
  const [copiedKey, setCopiedKey] = useState<string | null>(null);

  // Встроенный словарь соответствий для популярных NMS / Mojang маппингов (1.20 - 1.21)
  const defaultMappings: MappingEntry[] = [
    { obf: "net.minecraft.server.level.EntityPlayer", mojang: "net.minecraft.server.level.ServerPlayer", category: "Class", comment: "Основной класс игрока на сервере" },
    { obf: "net.minecraft.world.entity.player.EntityHuman", mojang: "net.minecraft.world.entity.player.Player", category: "Class", comment: "Базовый класс сущности человека" },
    { obf: "net.minecraft.world.level.WorldServer", mojang: "net.minecraft.server.level.ServerLevel", category: "Class", comment: "Серверный мир / измерение" },
    { obf: "net.minecraft.network.protocol.Packet", mojang: "net.minecraft.network.protocol.Packet", category: "Class", comment: "Сетевой пакет" },
    { obf: "net.minecraft.world.item.ItemStack", mojang: "net.minecraft.world.item.ItemStack", category: "Class", comment: "Предмет в инвентаре" },
    { obf: "net.minecraft.world.level.block.Block", mojang: "net.minecraft.world.level.block.Block", category: "Class", comment: "Блок мира" },
    { obf: "net.minecraft.nbt.NBTTagCompound", mojang: "net.minecraft.nbt.CompoundTag", category: "Class", comment: "NBT составной тег" },
    { obf: "a(Lnet/minecraft/world/entity/Entity;)V", mojang: "attack(Entity target)", category: "Method", comment: "Метод атаки сущности" },
    { obf: "k()Z", mojang: "isAlive()", category: "Method", comment: "Проверка жива ли сущность" },
    { obf: "d(Lnet/minecraft/core/BlockPosition;)V", mojang: "destroyBlock(BlockPos pos)", category: "Method", comment: "Разрушение блока" },
    { obf: "o()Lnet/minecraft/nbt/CompoundTag;", mojang: "saveWithoutId()", category: "Method", comment: "Сохранение NBT данных" },
    { obf: "net.minecraft.world.entity.ai.goal.PathfinderGoal", mojang: "net.minecraft.world.entity.ai.goal.Goal", category: "Class", comment: "Цель искусственного интеллекта моба" },
    { obf: "net.minecraft.world.entity.monster.EntityZombie", mojang: "net.minecraft.world.entity.monster.Zombie", category: "Class", comment: "Моб Зомби" },
    { obf: "net.minecraft.world.entity.boss.enderdragon.EntityEnderDragon", mojang: "net.minecraft.world.entity.boss.enderdragon.EnderDragon", category: "Class", comment: "Эндер Дракон" }
  ];

  if (!open) return null;

  const filtered = defaultMappings.filter((m) =>
    m.obf.toLowerCase().includes(search.toLowerCase()) ||
    m.mojang.toLowerCase().includes(search.toLowerCase()) ||
    (m.comment && m.comment.toLowerCase().includes(search.toLowerCase()))
  );

  function copyText(text: string, key: string) {
    navigator.clipboard.writeText(text);
    setCopiedKey(key);
    setTimeout(() => setCopiedKey(null), 1500);
  }

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm p-4">
      <div className="bg-panel border border-edge rounded-2xl w-full max-w-4xl max-h-[85vh] flex flex-col shadow-2xl overflow-hidden animate-in fade-in zoom-in-95 duration-150">
        {/* Header */}
        <div className="px-6 py-4 border-b border-edge flex items-center justify-between bg-surface/50">
          <div className="flex items-center gap-3">
            <div className="p-2 rounded-xl bg-blue-500/10 text-blue-400">
              <BookOpen size={22} />
            </div>
            <div>
              <h2 className="text-lg font-bold text-ink">Справочник маппингов (Mojang / Spigot / NMS)</h2>
              <p className="text-xs text-ink/60">Автоматический демаппинг обфусцированных классов и NMS методов на 1.20–1.21.x</p>
            </div>
          </div>
          <button onClick={onClose} className="p-1.5 rounded-lg text-ink/50 hover:text-ink hover:bg-surface transition-colors">
            <X size={18} />
          </button>
        </div>

        {/* Search */}
        <div className="px-6 py-3 border-b border-edge bg-surface/30 flex items-center gap-4">
          <div className="relative flex-1">
            <Search size={14} className="absolute left-3 top-1/2 -translate-y-1/2 text-ink/40" />
            <input
              type="text"
              placeholder="Поиск по NMS имени, Mojang имени или описанию..."
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              className="w-full bg-panel border border-edge rounded-lg pl-9 pr-3 py-1.5 text-xs text-ink placeholder:text-ink/40 focus:outline-none focus:border-accent"
            />
          </div>
          <div className="text-xs text-ink/60">
            Записей: <span className="font-semibold text-ink">{filtered.length}</span>
          </div>
        </div>

        {/* List */}
        <div className="p-6 overflow-y-auto flex-1 space-y-2">
          {filtered.length === 0 ? (
            <div className="py-12 text-center text-xs text-ink/50">Совпадений не найдено</div>
          ) : (
            filtered.map((m, idx) => (
              <div
                key={idx}
                className="p-3 rounded-xl border border-edge bg-surface/30 hover:bg-surface/60 transition-colors flex items-center justify-between gap-4"
              >
                <div className="flex-1 min-w-0 space-y-1">
                  <div className="flex items-center gap-2">
                    <span className="px-1.5 py-0.5 rounded text-[9px] font-bold bg-surface text-ink/60 border border-edge">
                      {m.category}
                    </span>
                    <span className="text-xs font-mono text-red-300 truncate">{m.obf}</span>
                    <ArrowRight size={13} className="text-ink/30 shrink-0" />
                    <span className="text-xs font-mono font-medium text-emerald-400 truncate">{m.mojang}</span>
                  </div>
                  {m.comment && <p className="text-[11px] text-ink/50">{m.comment}</p>}
                </div>

                <button
                  onClick={() => copyText(m.mojang, `${idx}`)}
                  className="p-1.5 rounded-lg border border-edge bg-panel hover:bg-surface text-ink/70 hover:text-ink transition-colors shrink-0"
                  title="Копировать Mojang имя"
                >
                  {copiedKey === `${idx}` ? <Check size={13} className="text-emerald-400" /> : <Copy size={13} />}
                </button>
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  );
}
