#include <cstdint>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>

static uint32_t get32(const uint8_t *p) {
    uint32_t v; std::memcpy(&v, p, sizeof(v)); return v;
}
static void put32(uint8_t *p, uint32_t v) { std::memcpy(p, &v, sizeof(v)); }
static void put64(uint8_t *p, uint64_t v) { std::memcpy(p, &v, sizeof(v)); }
static uint32_t parity(uint32_t v) {
    v ^= v >> 16; v ^= v >> 8; v ^= v >> 4;
    return ((~0x6996u) >> (v & 15u)) & 1u;
}
static uint32_t pkt4(uint32_t reg, uint32_t count) {
    return 0x40000000u | count | (parity(count) << 7) |
           ((reg & 0x3ffffu) << 8) | (parity(reg) << 27);
}
static uint32_t pkt7(uint32_t op, uint32_t count) {
    return 0x70000000u | count | (parity(count) << 15) |
           ((op & 127u) << 16) | (parity(op) << 23);
}

using Packet = std::array<uint32_t, 16>;
static Packet encode(const std::array<uint8_t, 52>& src,
                     uint32_t shared_reg, uint32_t vertex_reg,
                     uint32_t initiator16, uint32_t initiator32, bool visible) {
    const uint32_t format = get32(src.data()+12);
    const uint32_t width = format==57 ? 2u : format==42 ? 4u : 0u;
    if (!width) throw std::runtime_error("Unsupported DXGI index format");
    const uint64_t addr = uint64_t(get32(src.data())) |
                          (uint64_t(get32(src.data()+4)) << 32);
    if ((addr & (width-1)) != 0) throw std::runtime_error("Misaligned IBO");

    Packet packet{};
    packet[0] = pkt4(shared_reg, 4);
    for (int i=0;i<4;i++) packet[1+i] = get32(src.data()+16+4*i);
    packet[5] = pkt4(vertex_reg, 2);
    packet[6] = get32(src.data()+44);
    packet[7] = get32(src.data()+48);
    packet[8] = pkt7(0x38, 7);
    packet[9] = width==2 ? initiator16 : initiator32;
    packet[10] = get32(src.data()+36);
    packet[11] = visible ? get32(src.data()+32) : 0;
    packet[12] = get32(src.data()+40);
    packet[13] = get32(src.data()+0);
    packet[14] = get32(src.data()+4);
    packet[15] = get32(src.data()+8)/width;
    return packet;
}
int main() {
    std::array<uint8_t, 52> bytes{};
    put64(bytes.data(), 0x0000003abcd00000ull);
    put32(bytes.data()+8,8192);
    put32(bytes.data()+12,42);
    for (int i=0;i<4;i++) put32(bytes.data()+16+i*4,0xabcdef00+i);
    put32(bytes.data()+32,120);
    put32(bytes.data()+36,2);
    put32(bytes.data()+40,4);
    put32(bytes.data()+44,uint32_t(-6));
    put32(bytes.data()+48,3);
    const auto a = encode(bytes,0x1000,0x1100,0x1200,0x1400,true);
    assert(a[0]==pkt4(0x1000,4));
    assert(a[1]==0xabcdef00);
    assert(a[8]==pkt7(0x38,7));
    assert(a[9]==0x1400);
    assert(a[11]==120);
    assert(a[15]==2048);
    assert(encode(bytes,0x1000,0x1100,0x1200,0x1400,false)[11]==0);
    put32(bytes.data()+12,57);
    const auto b=encode(bytes,0x1000,0x1100,0x1200,0x1400,true);
    assert(b[9]==0x1200);
    assert(b[15]==4096);
    std::cout << "PASS: format + 52-byte stream + visibility + draw metadata\\n";
}
