#include "zip_container.hpp"

#include <zlib.h>
#include <algorithm>
#include <cstring>

namespace DocStudio {

namespace {

inline void AppendU16(std::vector<std::uint8_t>& dst, std::uint16_t v) {
    dst.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

inline void AppendU32(std::vector<std::uint8_t>& dst, std::uint32_t v) {
    dst.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    dst.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    dst.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

inline std::uint16_t ReadU16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

inline std::uint32_t ReadU32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool RawDeflate(const std::uint8_t* src, std::size_t len, std::vector<std::uint8_t>& out) {
    out.clear();
    if (len == 0) return true;
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(len);
    std::vector<std::uint8_t> buf(len + 128);
    for (;;) {
        zs.next_out = buf.data();
        zs.avail_out = static_cast<uInt>(buf.size());
        const int rc = deflate(&zs, Z_FINISH);
        const std::size_t have = buf.size() - zs.avail_out;
        out.insert(out.end(), buf.data(), buf.data() + have);
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK) {
            deflateEnd(&zs);
            return false;
        }
    }
    deflateEnd(&zs);
    return true;
}

bool RawInflate(const std::uint8_t* src, std::size_t len, std::uint32_t expectedLen, std::vector<std::uint8_t>& out) {
    out.clear();
    if (expectedLen == 0 && len == 0) return true;
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(len);
    const std::size_t chunkSz = (std::max<std::size_t>)(4096u, (std::min<std::size_t>)(expectedLen + 64u, 65536u));
    std::vector<std::uint8_t> buf(chunkSz);
    for (;;) {
        zs.next_out = buf.data();
        zs.avail_out = static_cast<uInt>(buf.size());
        const int rc = inflate(&zs, Z_NO_FLUSH);
        const std::size_t have = buf.size() - zs.avail_out;
        out.insert(out.end(), buf.data(), buf.data() + have);
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK && rc != Z_BUF_ERROR) {
            inflateEnd(&zs);
            return false;
        }
        if (zs.avail_in == 0 && rc == Z_BUF_ERROR) break;
        if (out.size() > 512ull * 1024ull * 1024ull) {
            inflateEnd(&zs);
            return false;
        }
    }
    inflateEnd(&zs);
    return true;
}

std::string NormalizeZipPath(std::string s) {
    for (char& ch : s) {
        if (ch == '\\') ch = '/';
    }
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    return s;
}

} // namespace

void ZipArchive::SetEntry(const std::string& pathUtf8, std::vector<std::uint8_t> data) {
    entries_[NormalizeZipPath(pathUtf8)] = std::move(data);
}

void ZipArchive::SetTextEntry(const std::string& pathUtf8, const std::string& textUtf8) {
    entries_[NormalizeZipPath(pathUtf8)] =
        std::vector<std::uint8_t>(textUtf8.begin(), textUtf8.end());
}

bool ZipArchive::HasEntry(const std::string& pathUtf8) const {
    return entries_.find(NormalizeZipPath(pathUtf8)) != entries_.end();
}

const std::vector<std::uint8_t>* ZipArchive::FindEntry(const std::string& pathUtf8) const {
    auto it = entries_.find(NormalizeZipPath(pathUtf8));
    return (it == entries_.end()) ? nullptr : &it->second;
}

std::vector<std::string> ZipArchive::ListEntries() const {
    std::vector<std::string> keys;
    keys.reserve(entries_.size());
    for (const auto& kv : entries_) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool ZipArchive::Serialize(std::vector<std::uint8_t>& outZipBytes, bool compress, std::string* outErr) const {
    outZipBytes.clear();
    std::vector<std::string> keys = ListEntries();

    struct CdRecord {
        std::string name;
        std::uint16_t method = 0;
        std::uint32_t crc = 0;
        std::uint32_t compSize = 0;
        std::uint32_t uncompSize = 0;
        std::uint32_t localOffset = 0;
    };
    std::vector<CdRecord> cd;
    cd.reserve(keys.size());

    for (const auto& name : keys) {
        const auto& raw = entries_.at(name);
        if (raw.size() > 0xFFFFFFFFull || name.size() > 0xFFFFu) {
            if (outErr) *outErr = "ZIP entry exceeds 32-bit size limit";
            return false;
        }
        const std::uint32_t uncompSz = static_cast<std::uint32_t>(raw.size());
        const std::uint32_t crc = static_cast<std::uint32_t>(
            crc32(0L, raw.empty() ? Z_NULL : raw.data(), uncompSz));

        std::uint16_t method = 0;
        const std::uint8_t* payloadPtr = raw.data();
        std::uint32_t payloadSz = uncompSz;
        std::vector<std::uint8_t> deflated;
        if (compress && !raw.empty()) {
            if (RawDeflate(raw.data(), raw.size(), deflated) && deflated.size() < raw.size()) {
                method = 8;
                payloadPtr = deflated.data();
                payloadSz = static_cast<std::uint32_t>(deflated.size());
            }
        }

        CdRecord rec{};
        rec.name = name;
        rec.method = method;
        rec.crc = crc;
        rec.compSize = payloadSz;
        rec.uncompSize = uncompSz;
        rec.localOffset = static_cast<std::uint32_t>(outZipBytes.size());
        cd.push_back(rec);

        // Local File Header
        AppendU32(outZipBytes, 0x04034B50u);
        AppendU16(outZipBytes, 20);
        AppendU16(outZipBytes, 0x0800u); // UTF-8 filename flag
        AppendU16(outZipBytes, method);
        AppendU16(outZipBytes, 0);       // mod time
        AppendU16(outZipBytes, 0x5421u); // mod date
        AppendU32(outZipBytes, crc);
        AppendU32(outZipBytes, payloadSz);
        AppendU32(outZipBytes, uncompSz);
        AppendU16(outZipBytes, static_cast<std::uint16_t>(name.size()));
        AppendU16(outZipBytes, 0);
        outZipBytes.insert(outZipBytes.end(), name.begin(), name.end());
        if (payloadSz > 0) {
            outZipBytes.insert(outZipBytes.end(), payloadPtr, payloadPtr + payloadSz);
        }
    }

    const std::uint32_t cdOffset = static_cast<std::uint32_t>(outZipBytes.size());
    for (const auto& rec : cd) {
        AppendU32(outZipBytes, 0x02014B50u);
        AppendU16(outZipBytes, 20);
        AppendU16(outZipBytes, 20);
        AppendU16(outZipBytes, 0x0800u);
        AppendU16(outZipBytes, rec.method);
        AppendU16(outZipBytes, 0);
        AppendU16(outZipBytes, 0x5421u);
        AppendU32(outZipBytes, rec.crc);
        AppendU32(outZipBytes, rec.compSize);
        AppendU32(outZipBytes, rec.uncompSize);
        AppendU16(outZipBytes, static_cast<std::uint16_t>(rec.name.size()));
        AppendU16(outZipBytes, 0);
        AppendU16(outZipBytes, 0);
        AppendU16(outZipBytes, 0);
        AppendU16(outZipBytes, 0);
        AppendU32(outZipBytes, 0);
        AppendU32(outZipBytes, rec.localOffset);
        outZipBytes.insert(outZipBytes.end(), rec.name.begin(), rec.name.end());
    }
    const std::uint32_t cdSize = static_cast<std::uint32_t>(outZipBytes.size()) - cdOffset;

    // End of Central Directory (EOCD)
    AppendU32(outZipBytes, 0x06054B50u);
    AppendU16(outZipBytes, 0);
    AppendU16(outZipBytes, 0);
    AppendU16(outZipBytes, static_cast<std::uint16_t>(cd.size()));
    AppendU16(outZipBytes, static_cast<std::uint16_t>(cd.size()));
    AppendU32(outZipBytes, cdSize);
    AppendU32(outZipBytes, cdOffset);
    AppendU16(outZipBytes, 0);
    return true;
}

bool ZipArchive::Deserialize(const std::uint8_t* bytes, std::size_t size, std::string* outErr) {
    entries_.clear();
    if (!bytes || size < 22) {
        if (outErr) *outErr = "Corrupt or empty ZIP buffer";
        return false;
    }

    // Locate EOCD scanning backwards within comment limit
    std::size_t eocdPos = std::string::npos;
    const std::size_t minSearch = (size > 65557u) ? (size - 65557u) : 0;
    for (std::size_t i = size - 22; ; --i) {
        if (ReadU32(bytes + i) == 0x06054B50u) {
            eocdPos = i;
            break;
        }
        if (i == minSearch) break;
    }
    if (eocdPos == std::string::npos) {
        if (outErr) *outErr = "ZIP EOCD signature not found";
        return false;
    }

    const std::uint16_t totalEntries = ReadU16(bytes + eocdPos + 10);
    const std::uint32_t cdSize = ReadU32(bytes + eocdPos + 12);
    const std::uint32_t cdOffset = ReadU32(bytes + eocdPos + 16);
    if (static_cast<std::uint64_t>(cdOffset) + cdSize > eocdPos) {
        if (outErr) *outErr = "ZIP central directory out of bounds";
        return false;
    }

    std::size_t pos = cdOffset;
    for (std::uint16_t i = 0; i < totalEntries; ++i) {
        if (pos + 46 > size || ReadU32(bytes + pos) != 0x02014B50u) {
            if (outErr) *outErr = "Corrupt ZIP central directory entry";
            return false;
        }
        const std::uint16_t method = ReadU16(bytes + pos + 10);
        const std::uint32_t expectedCrc = ReadU32(bytes + pos + 16);
        const std::uint32_t compSize = ReadU32(bytes + pos + 20);
        const std::uint32_t uncompSize = ReadU32(bytes + pos + 24);
        const std::uint16_t nameLen = ReadU16(bytes + pos + 28);
        const std::uint16_t extraLen = ReadU16(bytes + pos + 30);
        const std::uint16_t commentLen = ReadU16(bytes + pos + 32);
        const std::uint32_t localOffset = ReadU32(bytes + pos + 42);
        if (pos + 46 + nameLen + extraLen + commentLen > size) {
            if (outErr) *outErr = "Truncated ZIP CD record";
            return false;
        }
        std::string name(reinterpret_cast<const char*>(bytes + pos + 46), nameLen);
        pos += 46 + nameLen + extraLen + commentLen;

        if (localOffset + 30 > size || ReadU32(bytes + localOffset) != 0x04034B50u) {
            if (outErr) *outErr = "Corrupt ZIP local file header";
            return false;
        }
        const std::uint16_t lNameLen = ReadU16(bytes + localOffset + 26);
        const std::uint16_t lExtraLen = ReadU16(bytes + localOffset + 28);
        const std::size_t dataStart = static_cast<std::size_t>(localOffset) + 30 + lNameLen + lExtraLen;
        if (dataStart + compSize > size) {
            if (outErr) *outErr = "Truncated ZIP entry payload";
            return false;
        }

        std::vector<std::uint8_t> decoded;
        if (method == 0) {
            decoded.assign(bytes + dataStart, bytes + dataStart + compSize);
        } else if (method == 8) {
            if (!RawInflate(bytes + dataStart, compSize, uncompSize, decoded)) {
                if (outErr) *outErr = "Failed inflating ZIP entry: " + name;
                return false;
            }
        } else {
            if (outErr) *outErr = "Unsupported ZIP compression method";
            return false;
        }

        const std::uint32_t actualCrc = static_cast<std::uint32_t>(
            crc32(0L, decoded.empty() ? Z_NULL : decoded.data(), static_cast<uInt>(decoded.size())));
        if (actualCrc != expectedCrc || decoded.size() != uncompSize) {
            if (outErr) *outErr = "ZIP CRC32 or size mismatch on entry: " + name;
            return false;
        }
        entries_[NormalizeZipPath(name)] = std::move(decoded);
    }
    return true;
}

} // namespace DocStudio
