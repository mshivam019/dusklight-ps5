// SPDX-License-Identifier: MIT
// Synthetic disc tests. No game data is embedded here.
#include <nod.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

struct Stream { std::vector<uint8_t> bytes; int closes{}; size_t chunk{7}; };
static int64_t readAt(void* user, uint64_t offset, void* out, size_t count) {
    auto& s = *static_cast<Stream*>(user);
    if (offset > s.bytes.size()) return -1;
    count = std::min({count, s.bytes.size() - size_t(offset), s.chunk});
    memcpy(out, s.bytes.data() + offset, count); return count;
}
static int64_t length(void* user) { return static_cast<Stream*>(user)->bytes.size(); }
static void close(void* user) { ++static_cast<Stream*>(user)->closes; }
static void be32(Stream& s, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) s.bytes[offset + i] = value >> (24 - 8 * i);
}
static Stream valid() {
    Stream s; s.bytes.resize(0x804);
    be32(s, 0x1c, 0xc2339f3d); be32(s, 0x420, 0x500);
    be32(s, 0x424, 0x600); be32(s, 0x428, 35);
    be32(s, 0x600, 0x01000000); be32(s, 0x608, 2);
    be32(s, 0x60c, 1); be32(s, 0x610, 0x800); be32(s, 0x614, 4);
    memcpy(s.bytes.data() + 0x619, "asset.bin", 10);
    memcpy(s.bytes.data() + 0x800, "test", 4);
    return s;
}
static NodResult open(Stream& s, NodHandle** out) {
    const NodDiscStream stream{&s, readAt, length, close};
    return nod_disc_open_stream(&stream, nullptr, out);
}
static uint32_t visit(uint32_t i, NodNodeKind kind, const char* name, uint32_t size, void* user) {
    assert(i == 1 && kind == NOD_NODE_KIND_FILE && size == 4 && strcmp(name, "asset.bin") == 0);
    ++*static_cast<int*>(user); return i + 1;
}
int main() {
    auto s = valid(); NodHandle* disc{}; NodHandle* part{}; NodHandle* file{};
    assert(open(s, &disc) == NOD_RESULT_OK);
    assert(nod_disc_size(disc) == s.bytes.size());
    assert(nod_disc_open_partition_kind(disc, NOD_PARTITION_KIND_DATA, nullptr, &part) == NOD_RESULT_OK);
    int visited = 0; nod_partition_iterate_fst(part, visit, &visited); assert(visited == 1);
    NodPartitionMeta meta{}; assert(nod_partition_meta(part, &meta) == NOD_RESULT_OK);
    assert(meta.raw_dol.size == 0x100 && meta.raw_fst.size == 35);
    assert(nod_partition_open_file(part, 1, &file) == NOD_RESULT_OK);
    nod_free(disc); nod_free(part); assert(s.closes == 0);
    uint8_t result[8]{}; assert(nod_read(file, result, 8) == 4 && memcmp(result, "test", 4) == 0);
    assert(nod_read(file, result, 8) == 0);
    assert(nod_seek(file, -2, 2) == 2); assert(nod_read(file, result, 8) == 2 && memcmp(result, "st", 2) == 0);
    assert(nod_seek(file, -5, 0) == -1); assert(nod_seek(file, INT64_MIN, 1) == -1);
    assert(nod_seek(file, INT64_MAX, 2) == -1); assert(nod_seek(file, 0, 0) == 0);
    nod_free(file); assert(s.closes == 1);
    for (int fault = 0; fault < 6; ++fault) {
        auto bad = valid();
        if (fault == 0) be32(bad, 0x1c, 0);
        if (fault == 1) be32(bad, 0x428, UINT32_MAX);
        if (fault == 2) be32(bad, 0x608, UINT32_MAX);
        if (fault == 3) be32(bad, 0x610, UINT32_MAX);
        if (fault == 4) memset(bad.bytes.data() + 0x618, 'x', 11);
        if (fault == 5) be32(bad, 0x500, 0xfffffff0), be32(bad, 0x590, 0x1000);
        disc = reinterpret_cast<NodHandle*>(1);
        assert(open(bad, &disc) == NOD_RESULT_ERR_FORMAT && disc == nullptr && bad.closes == 1);
    }
    std::cout << "Raw GameCube reader: partial reads, handle lifetime, FST, seek bounds and malformed images passed\n";
}
