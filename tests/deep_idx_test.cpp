#include <vector>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
struct HT { unsigned char sha1[20]; int64_t usize; HT():usize(0){std::memset(sha1,0,20);} HT(const unsigned char*s,int64_t n):usize(n){std::memcpy(sha1,s,20);} };
class HTIndex { public: HTIndex(std::vector<HT>&h,unsigned):h_(h){} unsigned find(const char*s){for(unsigned i=1;i<h_.size();++i)if(!std::memcmp(h_[i].sha1,s,20))return i;return 0;} void update(){} private:std::vector<HT>&h_; };
#include "../src/oec_deep.hpp"
static void sha(unsigned char out[20],int x){for(int i=0;i<20;++i)out[i]=(unsigned char)(x*31+i*7);}
static void envset(const char*n,const std::string&v){setenv(n,v.c_str(),1);}
int main(){
  std::string zero="deep-zero.bin", idx="deep.idx", err; FILE*f=fopen(zero.c_str(),"wb");for(int i=0;i<10000;++i)fputc(i&255,f);fclose(f);
  std::vector<oecidx::FileInput> fs; if(!oecidx::write_cache_v2(idx,zero,"L","I",fs,err)){fprintf(stderr,"write %s\n",err.c_str());return 1;}
  std::vector<HT> ht(1); unsigned char a[20],b[20],c[20];sha(a,1);sha(b,2);sha(c,3);ht.push_back(HT(a,111));ht.push_back(HT(b,222));
  envset("ZPAQOEC_DEEP_IDX",idx);envset("ZPAQOEC_IDX_MEMORY","1M");
  {OecHybridHTIndex x(ht,64); if(x.find((char*)a)!=1||x.find((char*)b)!=2)return 2;ht.push_back(HT(c,333));x.update();if(x.find((char*)c)!=3)return 3;x.commit();}
  {OecHybridHTIndex x(ht,64);if(x.find((char*)c)!=3)return 4;}
  // Metadata refresh must preserve the deep EOF section.
  if(!oecidx::write_cache_v2(idx,zero,"L2","I2",fs,err)) return 41;
  {OecHybridHTIndex x(ht,64);if(x.find((char*)c)!=3)return 42;}
  // Crash simulation: add D in current process but do not commit, then authoritative HT rolls back.
  unsigned char d[20];sha(d,4);{OecHybridHTIndex x(ht,64);ht.push_back(HT(d,444));x.update();if(x.find((char*)d)!=4)return 5;}
  ht.resize(4); // rollback to committed archive state (0,A,B,C)
  {OecHybridHTIndex x(ht,64);if(x.find((char*)d)!=0)return 6;if(x.find((char*)c)!=3)return 7;x.commit();}
  oecidx::Cache cache;if(!cache.open(idx,zero,err)){fprintf(stderr,"cache %s\n",err.c_str());return 8;}
  remove(idx.c_str());remove(zero.c_str());puts("OEC DEEP IDX TESTS PASS");return 0;
}
