#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <iostream>
struct Layout { uint32_t stride,ib,constants,draw; };
static uint32_t u32(const uint8_t *p) { uint32_t r; std::memcpy(&r,p,4); return r; }
static void wr(uint8_t *p,uint32_t v) { std::memcpy(p,&v,4); }
static uint32_t parity(uint32_t v) {
    v^=v>>16;v^=v>>8;v^=v>>4;
    return ((~0x6996u)>>(v&15u))&1u;
}
static uint32_t pkt4(uint32_t r,uint32_t n) {
    return 0x40000000u|n|(parity(n)<<7)|((r&0x3ffffu)<<8)|(parity(r)<<27);
}
static uint32_t pkt7(uint32_t o,uint32_t n) {
    return 0x70000000u|n|(parity(n)<<15)|((o&127u)<<16)|(parity(o)<<23);
}
static std::array<uint32_t,16> encode(const uint8_t *src,Layout l,
    uint32_t sequence,uint32_t count,uint32_t reg,uint32_t vs,
    uint32_t draw16,uint32_t draw32) {
    const auto ib=src+l.ib,pc=src+l.constants,dr=src+l.draw;
    const uint32_t format=u32(ib+12), width=format==57u?2u:4u;
    const uint64_t address=(uint64_t(u32(ib+4))<<32)|u32(ib);
    const bool valid=sequence<count && (format==57u||format==42u) &&
        (address&(width-1u))==0;
    std::array<uint32_t,16> out{};
    out[0]=pkt4(reg,4);
    for(int i=0;i<4;i++)out[i+1]=u32(pc+4*i);
    out[5]=pkt4(vs,2);
    out[6]=u32(dr+12);out[7]=u32(dr+16);
    out[8]=pkt7(0x38,7);out[9]=format==57u?draw16:draw32;
    out[10]=valid?u32(dr+4):0;out[11]=valid?u32(dr):0;
    out[12]=u32(dr+8);
    out[13]=valid?u32(ib):0;out[14]=valid?u32(ib+4):0;
    out[15]=valid?u32(ib+8)/width:0;
    return out;
}
static void ok(bool value) { if(!value)throw std::runtime_error("DGC PM4 mismatch"); }
int main() {
    for(auto layout:{Layout{52,0,16,32},Layout{96,32,0,48}}) {
        std::array<uint8_t,96> input{};
        auto ib=input.data()+layout.ib,pc=input.data()+layout.constants,dr=input.data()+layout.draw;
        wr(ib,0x10000);wr(ib+4,0x7f);wr(ib+8,800);wr(ib+12,57);
        for(int i=0;i<4;i++)wr(pc+4*i,100+i);
        wr(dr,300);wr(dr+4,2);wr(dr+8,13);wr(dr+12,uint32_t(-9));wr(dr+16,7);
        auto a=encode(input.data(),layout,0,1,0x8100,0x8800,0x1234,0x4321);
        ok(a[0]==pkt4(0x8100,4)&&a[5]==pkt4(0x8800,2));
        ok(a[1]==100&&a[4]==103&&a[6]==uint32_t(-9)&&a[7]==7);
        ok(a[8]==pkt7(0x38,7)&&a[9]==0x1234);
        ok(a[10]==2&&a[11]==300&&a[12]==13);
        ok(a[13]==0x10000&&a[14]==0x7f&&a[15]==400);
        a=encode(input.data(),layout,2,1,0x8100,0x8800,0x1234,0x4321);
        ok(a[10]==0&&a[11]==0&&a[13]==0);
        wr(ib+12,42);
        a=encode(input.data(),layout,0,1,0x8100,0x8800,0x1234,0x4321);
        ok(a[9]==0x4321&&a[15]==200);
        wr(ib+12,91);
        a=encode(input.data(),layout,0,1,0x8100,0x8800,0x1234,0x4321);
        ok(a[11]==0&&a[13]==0);
    }
    std::cout<<"PASS: Vulkan DGC layout tokens, count, formats, root constants, Adreno PM4 fields\\n";
}
