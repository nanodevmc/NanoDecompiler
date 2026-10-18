import * as fs from "fs";
import * as zlib from "zlib";
import { promisify } from "util";

// Общий (раньше было ТРИ копии: jarSummary.ts, pluginTools.ts и inline в
// archiveExtractor.ts) читатель ZIP/JAR по central directory для
// НЕДОВЕРЕННЫХ файлов. Все размеры и смещения берутся из заголовков,
// которые полностью контролирует автор архива, поэтому здесь:
//   - размер central directory и сжатых данных ограничен сверху и сверяется
//     с реальным размером файла (раньше Buffer.alloc(cdSize) на 4 ГиБ
//     валил main-процесс по памяти от крошечного файла);
//   - чтение ВСЕГДА добирает ровно запрошенное число байт (fh.read мог
//     вернуть меньше, остаток молча оставался нулями);
//   - распаковка ограничена по выходному размеру (zip-bomb).

const inflateRawAsync = promisify(zlib.inflateRaw) as (buf: Buffer, opts: zlib.ZlibOptions) => Promise<Buffer>;

export const MAX_CENTRAL_DIR_BYTES = 64 * 1024 * 1024;
export const MAX_ENTRY_COMPRESSED_BYTES = 256 * 1024 * 1024;
export const MAX_ENTRY_UNCOMPRESSED_BYTES = 256 * 1024 * 1024;

export type ZipEntry = {
  name: string;
  method: number;
  compSize: number;
  uncompSize: number;
  localOffset: number;
  crc32: number;
};

export class ZipUnsupportedError extends Error {}

async function readExact(fh: fs.promises.FileHandle, length: number, position: number): Promise<Buffer> {
  const buf = Buffer.alloc(length);
  let done = 0;
  while (done < length) {
    const { bytesRead } = await fh.read(buf, done, length - done, position + done);
    if (bytesRead === 0) throw new Error("неожиданный конец файла (повреждённый ZIP/jar?)");
    done += bytesRead;
  }
  return buf;
}

/** Список записей central directory. Бросает ZipUnsupportedError для ZIP64. */
export async function listZipEntries(zipPath: string): Promise<ZipEntry[]> {
  const fh = await fs.promises.open(zipPath, "r");
  try {
    const fileSize = (await fh.stat()).size;
    if (fileSize < 22) throw new Error("не ZIP/jar файл (слишком маленький)");

    // EOCD - минимум 22 байта, максимум 22 + 65535 (если есть comment).
    const searchSize = Math.min(fileSize, 65557);
    const tail = await readExact(fh, searchSize, fileSize - searchSize);
    let eocd = -1;
    for (let i = tail.length - 22; i >= 0; i--) {
      if (tail.readUInt32LE(i) === 0x06054b50) {
        eocd = i;
        break;
      }
    }
    if (eocd < 0) throw new Error("не найден EOCD (не ZIP/jar файл?)");

    const total = tail.readUInt16LE(eocd + 10);
    const cdSize = tail.readUInt32LE(eocd + 12);
    const cdOffset = tail.readUInt32LE(eocd + 16);
    if (total === 0xffff || cdSize === 0xffffffff || cdOffset === 0xffffffff) {
      throw new ZipUnsupportedError("ZIP64 - не поддержано в быстром пути");
    }
    if (cdSize > MAX_CENTRAL_DIR_BYTES) throw new Error("central directory слишком большой");
    if (cdOffset + cdSize > fileSize) throw new Error("central directory выходит за границы файла");

    const cd = await readExact(fh, cdSize, cdOffset);
    const entries: ZipEntry[] = [];
    let p = 0;
    while (entries.length < total && p + 46 <= cd.length) {
      if (cd.readUInt32LE(p) !== 0x02014b50) break; // рассинхрон - лучше остановиться, чем читать мусор
      const method = cd.readUInt16LE(p + 10);
      const crc32 = cd.readUInt32LE(p + 16);
      const compSize = cd.readUInt32LE(p + 20);
      const uncompSize = cd.readUInt32LE(p + 24);
      const nameLen = cd.readUInt16LE(p + 28);
      const extraLen = cd.readUInt16LE(p + 30);
      const commentLen = cd.readUInt16LE(p + 32);
      const localOffset = cd.readUInt32LE(p + 42);
      const end = p + 46 + nameLen + extraLen + commentLen;
      if (end > cd.length) break;
      const name = cd.toString("utf-8", p + 46, p + 46 + nameLen);
      entries.push({ name, method, compSize, uncompSize, localOffset, crc32 });
      p = end;
    }
    return entries;
  } finally {
    await fh.close();
  }
}

/**
 * Читает и распаковывает ОДНУ запись. Только local header даёт точное начало
 * данных (extra-поля там могут отличаться по длине от central directory).
 */
export async function readZipEntry(
  zipPath: string,
  entry: ZipEntry,
  maxUncompressed: number = MAX_ENTRY_UNCOMPRESSED_BYTES,
): Promise<Buffer> {
  if (entry.compSize > MAX_ENTRY_COMPRESSED_BYTES) throw new Error("запись слишком большая");
  const fh = await fs.promises.open(zipPath, "r");
  try {
    const fileSize = (await fh.stat()).size;
    if (entry.localOffset + 30 > fileSize) throw new Error("local header выходит за границы файла");
    const head = await readExact(fh, 30, entry.localOffset);
    if (head.readUInt32LE(0) !== 0x04034b50) throw new Error("битый local header");
    const nameLen = head.readUInt16LE(26);
    const extraLen = head.readUInt16LE(28);
    const dataOffset = entry.localOffset + 30 + nameLen + extraLen;
    if (dataOffset + entry.compSize > fileSize) throw new Error("данные записи выходят за границы файла");

    const raw = await readExact(fh, entry.compSize, dataOffset);
    if (entry.method === 0) {
      if (raw.length > maxUncompressed) throw new Error("запись превышает лимит размера");
      return raw;
    }
    if (entry.method === 8) {
      // maxOutputLength защищает от zip-bomb: при превышении zlib бросает RangeError.
      return await inflateRawAsync(raw, { maxOutputLength: maxUncompressed });
    }
    throw new Error(`неподдерживаемый метод сжатия ${entry.method}`);
  } finally {
    await fh.close();
  }
}
