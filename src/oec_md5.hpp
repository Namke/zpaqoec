#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace oecmd5 {

inline uint32_t rol(uint32_t x, uint32_t n){ return (x<<n)|(x>>(32-n)); }

struct Context {
  uint32_t h[4]; uint64_t bytes; unsigned char block[64]; size_t used;
  Context(){ reset(); }
  void reset(){ h[0]=0x67452301u; h[1]=0xefcdab89u; h[2]=0x98badcfeu; h[3]=0x10325476u; bytes=0; used=0; }
  static uint32_t load32(const unsigned char* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
  static void store32(unsigned char* p,uint32_t v){ p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24); }
  void transform(const unsigned char* p){
    static const uint32_t k[64]={
      0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
      0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
      0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
      0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
      0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
      0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
      0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
      0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u};
    static const unsigned char r[64]={
      7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
      5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
      4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
      6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    uint32_t w[16]; for(int i=0;i<16;++i) w[i]=load32(p+i*4);
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3];
    for(uint32_t i=0;i<64;++i){ uint32_t f,g;
      if(i<16){f=(b&c)|((~b)&d);g=i;}
      else if(i<32){f=(d&b)|((~d)&c);g=(5*i+1)&15;}
      else if(i<48){f=b^c^d;g=(3*i+5)&15;}
      else {f=c^(b|(~d));g=(7*i)&15;}
      uint32_t t=d; d=c; c=b; b=b+rol(a+f+k[i]+w[g],r[i]); a=t;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;
  }
  void update(const void* data,size_t n){ const unsigned char* p=(const unsigned char*)data; bytes+=n;
    while(n){ size_t take=64-used; if(take>n)take=n; std::memcpy(block+used,p,take); used+=take;p+=take;n-=take; if(used==64){transform(block);used=0;} }
  }
  void final(unsigned char out[16]){ uint64_t bits=bytes*8u; unsigned char pad[128]; std::memset(pad,0,sizeof(pad));pad[0]=0x80;
    size_t plen=(used<56)?(56-used):(120-used); update(pad,plen); unsigned char len[8]; for(int i=0;i<8;++i)len[i]=(unsigned char)(bits>>(8*i)); update(len,8);
    for(int i=0;i<4;++i)store32(out+i*4,h[i]); }
};

inline bool file(const std::string& path,std::string& hex,std::string& err){
  FILE* f=zfec::fopen_utf8(path,"rb"); if(!f){err="cannot open source for MD5: "+path;return false;} Context c; unsigned char buf[1024*1024];
  for(;;){size_t n=std::fread(buf,1,sizeof(buf),f); if(n)c.update(buf,n); if(n<sizeof(buf)){if(std::ferror(f)){std::fclose(f);err="read error while calculating MD5: "+path;return false;}break;}}
  std::fclose(f); unsigned char d[16];c.final(d);static const char* x="0123456789abcdef";hex.resize(32);for(int i=0;i<16;++i){hex[i*2]=x[d[i]>>4];hex[i*2+1]=x[d[i]&15];}return true;
}
}
