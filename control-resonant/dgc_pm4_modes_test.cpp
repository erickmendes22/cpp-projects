#include <array>
#include <cstdint>
#include <stdexcept>
#include <iostream>
using Out=std::array<uint32_t,20>;
static uint32_t parity(uint32_t v){v^=v>>16;v^=v>>8;v^=v>>4;return ((~0x6996u)>>(v&15))&1u;}
static uint32_t pkt4(uint32_t r,uint32_t n){return 0x40000000u|n|(parity(n)<<7)|((r&0x3ffff)<<8)|(parity(r)<<27);}
static uint32_t pkt7(uint32_t o,uint32_t n){return 0x70000000u|n|(parity(n)<<15)|((o&0x7f)<<16)|(parity(o)<<23);}
static Out encode(bool shared,uint32_t format) {
  uint32_t pc[4]={11,12,13,14}, draw[5]={300,2,13,0xfffffff7u,7};
  uint64_t index_va=0x7f00010000ull;
  Out out{};uint32_t b=0;
  if(!shared){
    out[0]=pkt4(0x8100,4);
    for(unsigned i=0;i<4;i++)out[i+1]=pc[i];
    b=5;
  }else{
    out[0]=pkt7(0x30,7);
    out[1]=0x0abbccdd;out[2]=out[3]=0;
    for(unsigned i=0;i<4;i++)out[i+4]=pc[i];
    b=8;
  }
  out[b]=pkt4(0x8800,2);
  out[b+1]=draw[3];out[b+2]=draw[4];
  out[b+3]=pkt7(0x38,7);
  out[b+4]=format==57?0x1234:0x4321;
  out[b+5]=draw[1];out[b+6]=draw[0];out[b+7]=draw[2];
  out[b+8]=uint32_t(index_va);out[b+9]=uint32_t(index_va>>32);
  out[b+10]=800/(format==57?2:4);
  if(!shared){out[16]=pkt7(0x10,3);}else{out[19]=pkt7(0x10,0);}
  return out;
}
static void ok(bool p){if(!p)throw std::runtime_error("DGC PM4 mode incorrect");}
int main(){
  for(bool shared:{false,true})for(uint32_t format:{42u,57u}){
    auto o=encode(shared,format);auto b=shared?8u:5u;
    ok(o[0]==(shared?pkt7(0x30,7):pkt4(0x8100,4)));
    ok(o[shared?4:1]==11&&o[shared?7:4]==14);
    ok(o[b]==pkt4(0x8800,2)&&o[b+1]==0xfffffff7u&&o[b+2]==7);
    ok(o[b+3]==pkt7(0x38,7)&&o[b+4]==(format==57?0x1234u:0x4321u));
    ok(o[b+5]==2&&o[b+6]==300&&o[b+7]==13);
    ok(o[b+10]==(format==57?400u:200u));
    ok(o[shared?19:16]==pkt7(0x10,shared?0u:3u));
  }
  std::cout<<"PASS: DGC preamble/shared CP_LOAD_STATE6 20-dword sequences\\n";
}
