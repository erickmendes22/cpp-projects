#include <array>
#include <cstdint>
#include <stdexcept>
#include <iostream>

using Out = std::array<uint32_t,20>;
static uint32_t parity(uint32_t v) {
  v ^= v >> 16; v ^= v >> 8; v ^= v >> 4;
  return ((~0x6996u) >> (v & 15)) & 1u;
}
static uint32_t pkt4(uint32_t r, uint32_t n) {
  return 0x40000000u | n | (parity(n) << 7) |
         ((r & 0x3ffffu) << 8) | (parity(r) << 27);
}
static uint32_t pkt7(uint32_t op, uint32_t n) {
  return 0x70000000u | n | (parity(n) << 15) |
         ((op & 127u) << 16) | (parity(op) << 23);
}
static void ok(bool value) {
  if (!value) throw std::runtime_error("invalid A830 PM4 DGC packet");
}
static Out encode(bool shared, uint32_t dxgi_index_format, bool visible) {
  constexpr uint64_t index_va = 0x0000007f00010000ull;
  constexpr uint64_t draw_va = 0x0000007f1234abcdull;
  constexpr uint32_t pc[4] = {11,12,13,14};
  Out out{};
  uint32_t at = 0;
  if (!shared) {
    out[0] = pkt4(0x8100, 4);
    for (uint32_t i=0;i<4;i++) out[i+1] = pc[i];
    at = 5;
  } else {
    out[0] = pkt7(0x30, 7);
    out[1] = 0x0abbccdd;
    for (uint32_t i=0;i<4;i++) out[i+4] = pc[i];
    at = 8;
  }

  out[at] = pkt7(0x2a, 9);    // CP_DRAW_INDIRECT_MULTI
  out[at+1] = dxgi_index_format==57 ? 0x1234 : 0x4321;
  out[at+2] = 0x0badc0de;    // indexed opcode + VS params offset
  out[at+3] = visible ? 1 : 0;
  out[at+4] = visible ? uint32_t(index_va) : 0;
  out[at+5] = visible ? uint32_t(index_va>>32) : 0;
  out[at+6] = visible ? 800/(dxgi_index_format==57?2:4) : 0;
  out[at+7] = uint32_t(draw_va);
  out[at+8] = uint32_t(draw_va>>32);
  out[at+9] = 20; // VkDrawIndexedIndirectCommand byte stride
  const uint32_t remain = 20-(at+10);
  out[at+10] = pkt7(0x10, remain-1); // CP_NOP
  return out;
}
static void validate_stream(const Out& cmd) {
  unsigned pos=0;
  while (pos<cmd.size()) {
    uint32_t header=cmd[pos];
    uint32_t type=header>>28;
    uint32_t payload = type==4 ? (header & 0x7f) :
                       type==7 ? (header & 0x3fff) : UINT32_MAX;
    ok(payload != UINT32_MAX && pos+1+payload<=cmd.size());
    pos+=1+payload;
  }
  ok(pos==cmd.size());
}
int main() {
  for (bool shared : {false,true})
  for (uint32_t format : {42u,57u})
  for (bool visible : {false,true}) {
    auto out=encode(shared,format,visible);
    uint32_t at = shared ? 8 : 5;
    validate_stream(out);
    ok(out[0] == (shared ? pkt7(0x30,7) : pkt4(0x8100,4)));
    ok(out[shared?4:1]==11 && out[shared?7:4]==14);
    ok(out[at] == pkt7(0x2a,9) &&
       out[at+1] == (format==57?0x1234u:0x4321u));
    ok(out[at+3] == (visible?1u:0u));
    ok(out[at+4] == (visible?0x10000u:0u));
    ok(out[at+5] == (visible?0x7fu:0u));
    ok(out[at+6] == (visible?(format==57?400u:200u):0u));
    ok(out[at+7] == 0x1234abcdu && out[at+8] == 0x7fu);
    ok(out[at+9] == 20u);
    ok(out[at+10] == pkt7(0x10u,shared?1u:4u));
  }
  std::cout << "PASS: direct A830 PM4, DRAW_INDIRECT_MULTI, root constants, count, NOP boundaries\n";
}
