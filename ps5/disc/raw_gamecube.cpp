// SPDX-License-Identifier: MIT
// Application disc backend for raw GameCube ISO only. Implements the nod C API
// subset used by Aurora/Borealis; no compressed images or Wii partitions.
#include <nod.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace {
thread_local std::string error;
NodResult fail(NodResult code, const char* message) { error = message; return code; }
uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
struct Entry { uint32_t name, offset, size; bool directory; };
struct Disc {
    NodDiscStream stream{};
    uint64_t size{};
    NodDiscHeader header{};
    std::mutex lock;
    std::vector<uint8_t> fst, dol;
    std::vector<Entry> entries;
    bool ownsStream{};
    ~Disc() { if (ownsStream) stream.close(stream.user_data); }
    bool exact(uint64_t offset, uint8_t* out, size_t length) {
        if (offset > size || length > size - offset) return false;
        std::lock_guard guard(lock);
        while (length) {
            const auto n = stream.read_at(stream.user_data, offset, out, length);
            if (n <= 0 || uint64_t(n) > length) return false;
            offset += n; out += n; length -= n;
        }
        return true;
    }
    const char* name(const Entry& e) const { return reinterpret_cast<const char*>(fst.data() + e.name); }
    bool load() {
        if (!exact(0, reinterpret_cast<uint8_t*>(&header), sizeof(header)) ||
            be32(header.gcn_magic) != 0xc2339f3d || be32(header.wii_magic) == 0x5d1c9ea3) return false;
        std::array<uint8_t, 12> boot{};
        if (!exact(0x420, boot.data(), boot.size())) return false;
        const uint32_t dolOffset = be32(boot.data());
        const uint32_t fstOffset = be32(boot.data() + 4), fstSize = be32(boot.data() + 8);
        if (fstSize < 12 || fstSize > 64 * 1024 * 1024 || fstOffset > size || fstSize > size - fstOffset) return false;
        fst.resize(fstSize);
        if (!exact(fstOffset, fst.data(), fst.size())) return false;
        const uint32_t count = be32(fst.data() + 8);
        if (count == 0 || count > fstSize / 12 || fst[0] != 1) return false;
        const uint32_t names = count * 12;
        std::vector<uint32_t> directoryEnds{count};
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* p = fst.data() + i * 12;
            if (p[0] > 1) return false;
            const uint32_t stringOffset = be32(p) & 0xffffff;
            if (stringOffset >= fstSize - names) return false;
            Entry e{names + stringOffset, be32(p + 4), be32(p + 8), p[0] == 1};
            if (!memchr(fst.data() + e.name, 0, fstSize - e.name)) return false;
            if (i == 0) { if (e.offset != 0 || e.size != count) return false; }
            else {
                while (i >= directoryEnds.back()) directoryEnds.pop_back();
                if (e.directory) {
                    if (e.offset >= i || e.size <= i || e.size > directoryEnds.back()) return false;
                    directoryEnds.push_back(e.size);
                } else if (e.offset > size || e.size > size - e.offset) return false;
            }
            entries.push_back(e);
        }
        // DOL section ranges are offsets relative to the DOL, not load addresses.
        std::array<uint8_t, 0x100> dolHeader{};
        if (!exact(dolOffset, dolHeader.data(), dolHeader.size())) return false;
        uint64_t dolSize = dolHeader.size();
        for (size_t i = 0; i < 18; ++i) {
            const uint32_t offset = be32(dolHeader.data() + i * 4);
            const uint32_t length = be32(dolHeader.data() + 0x90 + i * 4);
            if (length) {
                if (offset < dolHeader.size()) return false;
                dolSize = std::max(dolSize, uint64_t(offset) + length);
            }
        }
        if (dolSize > 64 * 1024 * 1024 || dolOffset > size || dolSize > size - dolOffset) return false;
        dol.resize(dolSize);
        return exact(dolOffset, dol.data(), dol.size());
    }
};
}
struct NodHandle {
    enum class Kind { Disc, Partition, File } kind;
    std::shared_ptr<Disc> disc;
    uint64_t offset{}, size{}, position{};
};

extern "C" {
const char* nod_error_message() { return error.empty() ? nullptr : error.c_str(); }
NodResult nod_disc_open_stream(const NodDiscStream* stream, const NodDiscOptions*, NodHandle** out) {
    error.clear();
    if (out) *out = nullptr;
    if (!stream || !out || !stream->read_at || !stream->stream_len || !stream->close)
        return fail(NOD_RESULT_ERR_INVALID_HANDLE, "Invalid stream callbacks");
    // nod owns a valid stream even when format detection fails.
    bool adopted = false;
    try {
        auto disc = std::make_shared<Disc>();
        disc->stream = *stream; disc->ownsStream = true; adopted = true;
        const int64_t length = stream->stream_len(stream->user_data);
        if (length < 0) return fail(NOD_RESULT_ERR_IO, "Cannot determine disc length");
        disc->size = uint64_t(length);
        if (!disc->load()) return fail(NOD_RESULT_ERR_FORMAT, "Requires a valid raw GameCube ISO with bounded FST/DOL");
        *out = new NodHandle{NodHandle::Kind::Disc, disc, 0, disc->size};
        return NOD_RESULT_OK;
    } catch (...) {
        if (!adopted) stream->close(stream->user_data);
        return fail(NOD_RESULT_ERR_OTHER, "Cannot allocate disc metadata");
    }
}
void nod_free(NodHandle* handle) { delete handle; }
NodResult nod_disc_open_partition_kind(NodHandle* disc, uint32_t kind, const NodPartitionOptions*, NodHandle** out) {
    error.clear(); if (out) *out = nullptr;
    if (!disc || !out || disc->kind != NodHandle::Kind::Disc) return fail(NOD_RESULT_ERR_INVALID_HANDLE, "Invalid disc");
    if (kind != NOD_PARTITION_KIND_DATA) return fail(NOD_RESULT_ERR_NOT_FOUND, "GameCube has only a data partition");
    *out = new (std::nothrow) NodHandle{NodHandle::Kind::Partition, disc->disc, 0, disc->size};
    return *out ? NOD_RESULT_OK : fail(NOD_RESULT_ERR_OTHER, "Allocation failed");
}
NodResult nod_disc_open_partition(NodHandle* disc, uint32_t index, const NodPartitionOptions* options, NodHandle** out) {
    if (index != 0) { if (out) *out = nullptr; return fail(NOD_RESULT_ERR_NOT_FOUND, "Invalid GameCube partition index"); }
    return nod_disc_open_partition_kind(disc, NOD_PARTITION_KIND_DATA, options, out);
}
NodResult nod_partition_open_file(NodHandle* part, uint32_t index, NodHandle** out) {
    error.clear(); if (out) *out = nullptr;
    if (!part || !out || part->kind != NodHandle::Kind::Partition) return fail(NOD_RESULT_ERR_INVALID_HANDLE, "Invalid partition");
    if (index >= part->disc->entries.size() || part->disc->entries[index].directory) return fail(NOD_RESULT_ERR_NOT_FOUND, "Not a file");
    const auto& e = part->disc->entries[index];
    *out = new (std::nothrow) NodHandle{NodHandle::Kind::File, part->disc, e.offset, e.size};
    return *out ? NOD_RESULT_OK : fail(NOD_RESULT_ERR_OTHER, "Allocation failed");
}
int64_t nod_read(NodHandle* h, uint8_t* buffer, size_t length) {
    error.clear();
    if (!h || (!buffer && length)) { fail(NOD_RESULT_ERR_INVALID_HANDLE, "Invalid read"); return -1; }
    const size_t count = std::min<uint64_t>(length, h->size - h->position);
    if (!h->disc->exact(h->offset + h->position, buffer, count)) { fail(NOD_RESULT_ERR_IO, "Disc read failed"); return -1; }
    h->position += count; return count;
}
int64_t nod_seek(NodHandle* h, int64_t offset, int32_t whence) {
    error.clear();
    if (!h || whence < 0 || whence > 2) return -1;
    const uint64_t base = whence == 0 ? 0 : (whence == 1 ? h->position : h->size);
    uint64_t position;
    if (offset < 0) {
        const uint64_t negative = uint64_t(-(offset + 1)) + 1;
        if (negative > base) return -1;
        position = base - negative;
    } else {
        if (uint64_t(offset) > h->size - base) return -1;
        position = base + uint64_t(offset);
    }
    if (position > h->size || position > uint64_t(INT64_MAX)) return -1;
    h->position = position; return position;
}
NodResult nod_disc_header(const NodHandle* h, NodDiscHeader* out) {
    error.clear();
    if (!h || !out || h->kind != NodHandle::Kind::Disc) return fail(NOD_RESULT_ERR_INVALID_HANDLE, "Invalid disc header request");
    *out = h->disc->header; return NOD_RESULT_OK;
}
uint64_t nod_disc_size(const NodHandle* h) { return h && h->kind == NodHandle::Kind::Disc ? h->size : 0; }
NodResult nod_partition_meta(const NodHandle* h, NodPartitionMeta* out) {
    error.clear();
    if (!h || !out || h->kind != NodHandle::Kind::Partition) return fail(NOD_RESULT_ERR_INVALID_HANDLE, "Invalid partition metadata request");
    *out = {}; out->raw_fst = {h->disc->fst.data(), h->disc->fst.size()};
    out->raw_dol = {h->disc->dol.data(), h->disc->dol.size()}; return NOD_RESULT_OK;
}
void nod_partition_iterate_fst(const NodHandle* h, NodFstCallback callback, void* user) {
    error.clear();
    if (!h || !callback || h->kind != NodHandle::Kind::Partition) { fail(NOD_RESULT_ERR_INVALID_HANDLE, "Invalid FST iterator"); return; }
    // Like nod, root is implicit. A directory's size is its next sibling index.
    for (uint32_t i = 1; i < h->disc->entries.size();) {
        const auto& e = h->disc->entries[i];
        const uint32_t next = callback(i, e.directory ? NOD_NODE_KIND_DIRECTORY : NOD_NODE_KIND_FILE,
            h->disc->name(e), e.size, user);
        if (next == NOD_FST_STOP) return;
        if (next <= i) { fail(NOD_RESULT_ERR_OTHER, "FST callback must advance"); return; }
        i = next;
    }
}
}
