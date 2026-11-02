#include <memory>
#include "zip_writer.hpp"
#include "zip_reader.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <zlib.h>

namespace nd {

namespace {

std::vector<uint8_t> deflate_data(const std::vector<uint8_t>& in) {
    if (in.empty()) return {};
    z_stream strm{};
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    std::vector<uint8_t> out(in.size() + 1024);
    strm.next_in = const_cast<Bytef*>(in.data());
    strm.avail_in = static_cast<uInt>(in.size());
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out.size());
    deflate(&strm, Z_FINISH);
    out.resize(strm.total_out);
    deflateEnd(&strm);
    return out;
}

void wr_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void wr_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

}  // namespace

ZipWriteResult ZipWriter::patch_zip(const std::string& in_zip_path,
                                    const std::string& out_zip_path,
                                    const std::map<std::string, std::vector<uint8_t>>& replacements) {
    std::unique_ptr<ZipReader> reader;
    try {
        reader = std::make_unique<ZipReader>(in_zip_path);
    } catch (const std::exception& e) {
        return {false, std::string("Cannot open source jar: ") + e.what(), 0};
    }

    std::ofstream out(out_zip_path, std::ios::binary);
    if (!out.is_open()) {
        return {false, "Cannot open output jar for writing: " + out_zip_path, 0};
    }

    struct CdEntry {
        std::string name;
        uint32_t crc = 0;
        uint32_t comp_size = 0;
        uint32_t uncomp_size = 0;
        uint32_t local_hdr_offset = 0;
        uint16_t comp_method = 8;
    };

    std::vector<CdEntry> cd_entries;
    size_t replaced = 0;

    for (const auto& name : reader->namelist()) {
        CdEntry cd;
        cd.name = name;
        cd.local_hdr_offset = static_cast<uint32_t>(out.tellp());

        std::vector<uint8_t> data_to_write;
        auto rep_it = replacements.find(name);
        if (rep_it != replacements.end()) {
            replaced++;
            const auto& raw_bytes = rep_it->second;
            cd.uncomp_size = static_cast<uint32_t>(raw_bytes.size());
            cd.crc = static_cast<uint32_t>(crc32(0L, raw_bytes.data(), static_cast<uInt>(raw_bytes.size())));
            data_to_write = deflate_data(raw_bytes);
            cd.comp_size = static_cast<uint32_t>(data_to_write.size());
            cd.comp_method = 8;
        } else {
            auto raw = reader->read(name);
            cd.uncomp_size = static_cast<uint32_t>(raw.size());
            cd.crc = static_cast<uint32_t>(crc32(0L, raw.data(), static_cast<uInt>(raw.size())));
            data_to_write = deflate_data(raw);
            cd.comp_size = static_cast<uint32_t>(data_to_write.size());
            cd.comp_method = 8;
        }

        std::vector<uint8_t> lh;
        wr_u32(lh, 0x04034b50);
        wr_u16(lh, 20);
        wr_u16(lh, 0);
        wr_u16(lh, cd.comp_method);
        wr_u16(lh, 0);
        wr_u16(lh, 0);
        wr_u32(lh, cd.crc);
        wr_u32(lh, cd.comp_size);
        wr_u32(lh, cd.uncomp_size);
        wr_u16(lh, static_cast<uint16_t>(name.size()));
        wr_u16(lh, 0);
        out.write(reinterpret_cast<const char*>(lh.data()), lh.size());
        out.write(name.data(), name.size());
        out.write(reinterpret_cast<const char*>(data_to_write.data()), data_to_write.size());

        cd_entries.push_back(cd);
    }

    uint32_t cd_start = static_cast<uint32_t>(out.tellp());
    for (const auto& cd : cd_entries) {
        std::vector<uint8_t> cdh;
        wr_u32(cdh, 0x02014b50);
        wr_u16(cdh, 20);
        wr_u16(cdh, 20);
        wr_u16(cdh, 0);
        wr_u16(cdh, cd.comp_method);
        wr_u16(cdh, 0);
        wr_u16(cdh, 0);
        wr_u32(cdh, cd.crc);
        wr_u32(cdh, cd.comp_size);
        wr_u32(cdh, cd.uncomp_size);
        wr_u16(cdh, static_cast<uint16_t>(cd.name.size()));
        wr_u16(cdh, 0);
        wr_u16(cdh, 0);
        wr_u16(cdh, 0);
        wr_u16(cdh, 0);
        wr_u32(cdh, 0);
        wr_u32(cdh, cd.local_hdr_offset);
        out.write(reinterpret_cast<const char*>(cdh.data()), cdh.size());
        out.write(cd.name.data(), cd.name.size());
    }
    uint32_t cd_size = static_cast<uint32_t>(out.tellp()) - cd_start;

    std::vector<uint8_t> eocd;
    wr_u32(eocd, 0x06054b50);
    wr_u16(eocd, 0);
    wr_u16(eocd, 0);
    wr_u16(eocd, static_cast<uint16_t>(cd_entries.size()));
    wr_u16(eocd, static_cast<uint16_t>(cd_entries.size()));
    wr_u32(eocd, cd_size);
    wr_u32(eocd, cd_start);
    wr_u16(eocd, 0);
    out.write(reinterpret_cast<const char*>(eocd.data()), eocd.size());

    out.close();
    return {true, "Successfully repacked JAR archive", replaced};
}

}  // namespace nd
