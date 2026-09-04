#pragma once

// OEC cold cross-part protection (OECOLD1 / OECPAR1).
// Adjunct format only: never changes ZPAQ/ZFEC/IDX bytes.
// C++11, no external dependencies.

#include "zfec.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace oeccold {

static const uint32_t kVersion = 1;
static const uint32_t kParityHeaderSize = 128;

inline uint64_t ceil_div(uint64_t a,uint64_t b){return a/b+((a%b)?1:0);}
inline bool is_pow2(uint64_t x){return x && !(x&(x-1));}

// ---------- SHA-256 ----------
struct Sha256 {
  uint32_t h[8]; uint64_t bits; uint8_t buf[64]; size_t used;
  Sha256(){reset();}
  static uint32_t rotr(uint32_t x,uint32_t n){return (x>>n)|(x<<(32-n));}
  void reset(){h[0]=0x6a09e667;h[1]=0xbb67ae85;h[2]=0x3c6ef372;h[3]=0xa54ff53a;h[4]=0x510e527f;h[5]=0x9b05688c;h[6]=0x1f83d9ab;h[7]=0x5be0cd19;bits=0;used=0;}
  void block(const uint8_t* p){
    static const uint32_t K[64]={
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64]; for(int i=0;i<16;++i)w[i]=(uint32_t(p[i*4])<<24)|(uint32_t(p[i*4+1])<<16)|(uint32_t(p[i*4+2])<<8)|p[i*4+3];
    for(int i=16;i<64;++i){uint32_t a=w[i-15],b=w[i-2];uint32_t s0=rotr(a,7)^rotr(a,18)^(a>>3),s1=rotr(b,17)^rotr(b,19)^(b>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for(int i=0;i<64;++i){uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25),ch=(e&f)^((~e)&g),t1=hh+S1+ch+K[i]+w[i],S0=rotr(a,2)^rotr(a,13)^rotr(a,22),maj=(a&b)^(a&c)^(b&c),t2=S0+maj;hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
  }
  void update(const void* vp,size_t n){const uint8_t* p=(const uint8_t*)vp;bits+=uint64_t(n)*8;while(n){size_t m=std::min(n,64-used);std::memcpy(buf+used,p,m);used+=m;p+=m;n-=m;if(used==64){block(buf);used=0;}}}
  std::string final(){uint64_t orig=bits;uint8_t one=0x80;update(&one,1);uint8_t z=0;while(used!=56)update(&z,1);uint8_t be[8];for(int i=0;i<8;++i)be[7-i]=(uint8_t)(orig>>(i*8));update(be,8);std::ostringstream o;o<<std::hex<<std::setfill('0');for(int i=0;i<8;++i)o<<std::setw(8)<<h[i];return o.str();}
};
inline bool sha256_file(const std::string& p,std::string& out){FILE* f=zfec::fopen_utf8(p,"rb");if(!f)return false;Sha256 s;std::vector<uint8_t>b(1<<20);for(;;){size_t n=std::fread(b.data(),1,b.size(),f);if(n)s.update(b.data(),n);if(n<b.size()){if(std::ferror(f)){std::fclose(f);return false;}break;}}std::fclose(f);out=s.final();return true;}
inline std::string sha256_text(const std::string& s){Sha256 h;h.update(s.data(),s.size());return h.final();}

// ---------- text/path helpers ----------
inline std::string hex_encode(const std::string& s){static const char* d="0123456789abcdef";std::string o;o.reserve(s.size()*2);for(size_t i=0;i<s.size();++i){unsigned c=(unsigned char)s[i];o.push_back(d[c>>4]);o.push_back(d[c&15]);}return o;}
inline int hx(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
inline bool hex_decode(const std::string& s,std::string& o){if(s.size()%2)return false;o.clear();o.reserve(s.size()/2);for(size_t i=0;i<s.size();i+=2){int a=hx(s[i]),b=hx(s[i+1]);if(a<0||b<0)return false;o.push_back((char)((a<<4)|b));}return true;}
inline size_t sep_pos(const std::string& p){size_t a=p.find_last_of('/'),b=p.find_last_of('\\');if(a==std::string::npos)return b;if(b==std::string::npos)return a;return std::max(a,b);}
inline std::string dirname(const std::string& p){size_t s=sep_pos(p);return s==std::string::npos?std::string("."):p.substr(0,s);}
inline std::string basename(const std::string& p){size_t s=sep_pos(p);return s==std::string::npos?p:p.substr(s+1);}
inline std::string join(const std::string& a,const std::string& b){if(a.empty()||a==".")return a=="."?std::string("./")+b:b;char c=a[a.size()-1];return (c=='/'||c=='\\')?a+b:a+"/"+b;}
inline bool mkdir_one(const std::string& p){if(p.empty()||p=="."||p=="/"||zfec::file_exists(p))return true;return zfec::mkdir_utf8(p)==0 || errno==EEXIST;}
inline bool mkdirs(std::string p){if(p.empty()||p==".")return true;for(size_t i=0;i<p.size();++i)if(p[i]=='\\')p[i]='/';size_t start=0;
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  if(p.size()>=3 && p[1]==':' && p[2]=='/') start=3;
#else
  if(!p.empty()&&p[0]=='/')start=1;
#endif
  for(size_t i=start;i<=p.size();++i){if(i==p.size()||p[i]=='/'){std::string q=p.substr(0,i);if(!q.empty()&&!mkdir_one(q))return false;}}return true;}
inline bool copy_file(const std::string&a,const std::string&b){FILE* i=zfec::fopen_utf8(a,"rb");if(!i)return false;if(!mkdirs(dirname(b))){std::fclose(i);return false;}FILE* o=zfec::fopen_utf8(b,"wb");if(!o){std::fclose(i);return false;}std::vector<uint8_t>x(1<<20);bool ok=true;for(;;){size_t n=std::fread(x.data(),1,x.size(),i);if(n&&std::fwrite(x.data(),1,n,o)!=n){ok=false;break;}if(n<x.size()){if(std::ferror(i))ok=false;break;}}if(std::fclose(o)!=0)ok=false;std::fclose(i);if(!ok)zfec::remove_utf8(b);return ok;}
inline bool parse_size(const std::string&s,uint64_t&v){if(s.empty())return false;char* e=0;unsigned long long n=std::strtoull(s.c_str(),&e,10);if(e==s.c_str())return false;uint64_t m=1;if(*e){char c=(char)std::tolower((unsigned char)*e++);if(c=='k')m=1024ull;else if(c=='m')m=1024ull*1024;else if(c=='g')m=1024ull*1024*1024;else return false;if(*e=='b'||*e=='B')++e;}if(*e)return false;if(n>~uint64_t(0)/m)return false;v=uint64_t(n)*m;return true;}
inline bool write_atomic(const std::string& p,const std::string& body,std::string& err){if(!mkdirs(dirname(p))){err="cannot create manifest directory";return false;}std::string t=p+".tmp";FILE*f=zfec::fopen_utf8(t,"wb");if(!f){err="cannot create "+t;return false;}bool ok=body.empty()||std::fwrite(body.data(),1,body.size(),f)==body.size();if(std::fclose(f)!=0)ok=false;if(!ok){zfec::remove_utf8(t);err="cannot write "+t;return false;}zfec::remove_utf8(p);if(zfec::rename_utf8(t,p)!=0){zfec::remove_utf8(t);err="cannot install "+p;return false;}return true;}

// ---------- GF(256) matrices ----------
typedef std::vector<uint8_t> Matrix;
inline uint8_t gf_pow(uint8_t x,unsigned n){uint8_t r=1;while(n){if(n&1)r=zfec::gf().mul(r,x);x=zfec::gf().mul(x,x);n>>=1;}return r;}
inline bool mat_inv(const Matrix&a,unsigned n,Matrix&inv){Matrix x(n*n*2,0);for(unsigned r=0;r<n;++r){for(unsigned c=0;c<n;++c)x[r*(2*n)+c]=a[r*n+c];x[r*(2*n)+n+r]=1;}for(unsigned c=0;c<n;++c){unsigned piv=c;while(piv<n&&!x[piv*(2*n)+c])++piv;if(piv==n)return false;if(piv!=c)for(unsigned j=0;j<2*n;++j)std::swap(x[c*(2*n)+j],x[piv*(2*n)+j]);uint8_t pv=x[c*(2*n)+c];if(pv!=1)for(unsigned j=0;j<2*n;++j)x[c*(2*n)+j]=zfec::gf().div(x[c*(2*n)+j],pv);for(unsigned r=0;r<n;++r)if(r!=c){uint8_t f=x[r*(2*n)+c];if(f)for(unsigned j=0;j<2*n;++j)x[r*(2*n)+j]^=zfec::gf().mul(f,x[c*(2*n)+j]);}}inv.assign(n*n,0);for(unsigned r=0;r<n;++r)for(unsigned c=0;c<n;++c)inv[r*n+c]=x[r*(2*n)+n+c];return true;}
inline Matrix mat_mul(const Matrix&a,unsigned ar,unsigned ac,const Matrix&b,unsigned bc){Matrix o(ar*bc,0);for(unsigned r=0;r<ar;++r)for(unsigned c=0;c<bc;++c){uint8_t v=0;for(unsigned k=0;k<ac;++k)v^=zfec::gf().mul(a[r*ac+k],b[k*bc+c]);o[r*bc+c]=v;}return o;}
inline bool generator(unsigned k,unsigned m,Matrix&g){if(!k||k+m>255)return false;Matrix v((k+m)*k,0);for(unsigned r=0;r<k+m;++r){uint8_t x=(uint8_t)(r+1);for(unsigned c=0;c<k;++c)v[r*k+c]=gf_pow(x,c);}Matrix top(v.begin(),v.begin()+k*k),inv;if(!mat_inv(top,k,inv))return false;g=mat_mul(v,k+m,k,inv,k);return true;}
inline void linear(uint8_t*out,const std::vector<std::vector<uint8_t> >&src,const uint8_t*coef,unsigned k,size_t n){std::memset(out,0,n);for(unsigned i=0;i<k;++i)if(coef[i])for(size_t j=0;j<n;++j)out[j]^=zfec::gf().mul(coef[i],src[i][j]);}

#pragma pack(push,1)
struct ParityHeader {char magic[8];uint32_t version;uint32_t header_size;uint32_t group_id;uint32_t parity_index;uint32_t k;uint32_t m;uint32_t shard_size;uint64_t stripe_count;uint64_t payload_bytes;uint32_t header_crc;uint8_t reserved[72];};
#pragma pack(pop)
static_assert(sizeof(ParityHeader)==128,"ParityHeader ABI");
inline uint32_t parity_header_crc(ParityHeader h){h.header_crc=0;return zfec::crc32c(&h,sizeof(h));}

struct FileRec {std::string rel;uint64_t size;std::string hash;};
struct Group {uint32_t id;uint32_t k,m;uint64_t stripes;std::vector<FileRec> data,parity;std::vector<uint32_t> data_crc,parity_crc;};
struct Manifest {uint32_t version,max_k,m;uint64_t shard_size;std::string profile,grouping,hash_alg,data_root,parity_root,set_name;uint32_t manifest_copies;std::vector<Group> groups;};
struct SealOptions {uint32_t k,m;uint64_t shard;std::string profile,grouping,hash_alg,output,manifest,copy_dir;uint32_t copies;bool include_index,drop_part_ec,force,verify_after;SealOptions():k(20),m(2),shard(4ull<<20),profile("balanced"),grouping("size"),hash_alg("sha256"),copies(2),include_index(true),drop_part_ec(false),force(false),verify_after(true){}};
struct RootOverride {std::string data_root,parity_root;bool no_install;RootOverride():no_install(false){}};

inline bool valid_options(const SealOptions&o,std::string&err){if(o.k<1||o.k>128){err="--data must be 1..128";return false;}if(o.m<1||o.m>32){err="--parity must be 1..32";return false;}if(o.k+o.m>255){err="data+parity must be <=255";return false;}if(o.shard<65536||o.shard>(16ull<<20)||!is_pow2(o.shard)){err="--shard-size must be power-of-two 64K..16M";return false;}if(o.grouping!="size"&&o.grouping!="sequential"){err="--grouping must be size|sequential";return false;}if(o.hash_alg!="sha256"&&o.hash_alg!="none"){err="--hash must be sha256|none";return false;}if(o.copies>32){err="--manifest-copies must be 0..32";return false;}return true;}

inline std::string manifest_body(const Manifest&m){std::ostringstream o;o<<"OECOLD1\n"<<"version="<<m.version<<"\nmax_k="<<m.max_k<<"\nm="<<m.m<<"\nshard_size="<<m.shard_size<<"\nprofile="<<m.profile<<"\ngrouping="<<m.grouping<<"\nhash="<<m.hash_alg<<"\ndata_root="<<hex_encode(m.data_root)<<"\nparity_root="<<hex_encode(m.parity_root)<<"\nset_name="<<hex_encode(m.set_name)<<"\nmanifest_copies="<<m.manifest_copies<<"\ngroup_count="<<m.groups.size()<<"\n";
  for(size_t gi=0;gi<m.groups.size();++gi){const Group&g=m.groups[gi];o<<"G "<<g.id<<" "<<g.k<<" "<<g.m<<" "<<g.stripes<<"\n";for(size_t i=0;i<g.data.size();++i)o<<"D "<<g.id<<" "<<i<<" "<<hex_encode(g.data[i].rel)<<" "<<g.data[i].size<<" "<<(g.data[i].hash.empty()?"-":g.data[i].hash)<<"\n";for(size_t i=0;i<g.parity.size();++i)o<<"P "<<g.id<<" "<<i<<" "<<hex_encode(g.parity[i].rel)<<" "<<g.parity[i].size<<" "<<(g.parity[i].hash.empty()?"-":g.parity[i].hash)<<"\n";for(uint64_t s=0;s<g.stripes;++s){o<<"C "<<g.id<<" "<<s;for(unsigned i=0;i<g.k;++i)o<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<g.data_crc[(size_t)s*g.k+i]<<std::dec;for(unsigned p=0;p<g.m;++p)o<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<g.parity_crc[(size_t)s*g.m+p]<<std::dec;o<<"\n";}o<<"E "<<g.id<<"\n";}return o.str();}
inline std::string manifest_serialize(const Manifest&m){std::string b=manifest_body(m);return b+"MANIFEST_SHA256="+sha256_text(b)+"\n";}

inline bool parse_u64(const std::string&s,uint64_t&v){char*e=0;unsigned long long x=std::strtoull(s.c_str(),&e,10);if(!e||*e)return false;v=x;return true;}
inline bool parse_manifest_text(const std::string&txt,Manifest&m,std::string&err){size_t pos=txt.rfind("MANIFEST_SHA256=");if(pos==std::string::npos){err="manifest checksum missing";return false;}std::string body=txt.substr(0,pos),tail=txt.substr(pos+16);size_t nl=tail.find('\n');if(nl!=std::string::npos)tail.resize(nl);if(sha256_text(body)!=tail){err="manifest SHA-256 mismatch";return false;}std::istringstream in(body);std::string line;if(!std::getline(in,line)||line!="OECOLD1"){err="not OECOLD1";return false;}m=Manifest();m.version=0;m.max_k=0;m.m=0;m.shard_size=0;m.manifest_copies=0;std::map<uint32_t,size_t>gm;
  while(std::getline(in,line)){if(line.empty())continue;if(line[0]=='G'&&line.size()>1&&line[1]==' '){std::istringstream q(line);char c;Group g;q>>c>>g.id>>g.k>>g.m>>g.stripes;if(!q||!g.k||!g.m){err="bad G line";return false;}g.data.resize(g.k);g.parity.resize(g.m);g.data_crc.assign((size_t)g.stripes*g.k,0);g.parity_crc.assign((size_t)g.stripes*g.m,0);gm[g.id]=m.groups.size();m.groups.push_back(g);continue;}if((line[0]=='D'||line[0]=='P')&&line.size()>1&&line[1]==' '){std::istringstream q(line);char c;uint32_t id,ix;std::string hxv,hs;uint64_t sz;q>>c>>id>>ix>>hxv>>sz>>hs;if(!q||!gm.count(id)){err="bad file record";return false;}std::string rel;if(!hex_decode(hxv,rel)){err="bad path hex";return false;}Group&g=m.groups[gm[id]];FileRec r;r.rel=rel;r.size=sz;r.hash=hs=="-"?std::string():hs;if(c=='D'){if(ix>=g.k)return false;g.data[ix]=r;}else{if(ix>=g.m)return false;g.parity[ix]=r;}continue;}if(line[0]=='C'&&line.size()>1&&line[1]==' '){std::istringstream q(line);char c;uint32_t id;uint64_t s;q>>c>>id>>s;if(!q||!gm.count(id)){err="bad C line";return false;}Group&g=m.groups[gm[id]];if(s>=g.stripes){err="CRC stripe out of range";return false;}std::string x;for(unsigned i=0;i<g.k;++i){if(!(q>>x)){err="short data CRC line";return false;}g.data_crc[(size_t)s*g.k+i]=(uint32_t)std::strtoul(x.c_str(),0,16);}for(unsigned p=0;p<g.m;++p){if(!(q>>x)){err="short parity CRC line";return false;}g.parity_crc[(size_t)s*g.m+p]=(uint32_t)std::strtoul(x.c_str(),0,16);}continue;}size_t eq=line.find('=');if(eq!=std::string::npos){std::string k=line.substr(0,eq),v=line.substr(eq+1);uint64_t n=0;if(k=="version"){parse_u64(v,n);m.version=(uint32_t)n;}else if(k=="max_k"){parse_u64(v,n);m.max_k=(uint32_t)n;}else if(k=="m"){parse_u64(v,n);m.m=(uint32_t)n;}else if(k=="shard_size"){parse_u64(v,m.shard_size);}else if(k=="profile")m.profile=v;else if(k=="grouping")m.grouping=v;else if(k=="hash")m.hash_alg=v;else if(k=="manifest_copies"){parse_u64(v,n);m.manifest_copies=(uint32_t)n;}else if(k=="data_root"){if(!hex_decode(v,m.data_root))return false;}else if(k=="parity_root"){if(!hex_decode(v,m.parity_root))return false;}else if(k=="set_name"){if(!hex_decode(v,m.set_name))return false;}}}
  if(m.version!=1||!m.shard_size||m.groups.empty()){err="incomplete OECOLD1 manifest";return false;}return true;}
inline bool read_all(const std::string&p,std::string&x){FILE*f=zfec::fopen_utf8(p,"rb");if(!f)return false;x.clear();char b[65536];for(;;){size_t n=std::fread(b,1,sizeof(b),f);if(n)x.append(b,n);if(n<sizeof(b)){bool ok=!std::ferror(f);std::fclose(f);return ok;}}}
inline bool load_manifest(const std::string&p,Manifest&m,std::string&used,std::string&err){std::vector<std::string>cand;cand.push_back(p);for(unsigned i=1;i<=32;++i){std::ostringstream q;q<<p<<".copy"<<i;cand.push_back(q.str());}for(size_t i=0;i<cand.size();++i){std::string x;if(!read_all(cand[i],x))continue;std::string e;if(parse_manifest_text(x,m,e)){used=cand[i];return true;}if(i==0)err=e;}if(err.empty())err="manifest and replicas not readable: "+p;return false;}

inline bool read_shard(FILE*f,uint64_t off,uint64_t filesize,size_t shard,std::vector<uint8_t>&b){b.assign(shard,0);if(off>=filesize)return true;if(!zfec::seek64(f,off))return false;size_t want=(size_t)std::min<uint64_t>(shard,filesize-off);return std::fread(b.data(),1,want,f)==want;}
inline bool read_parity_shard(FILE*f,uint64_t stripe,size_t shard,std::vector<uint8_t>&b){b.assign(shard,0);uint64_t off=kParityHeaderSize+stripe*uint64_t(shard);return zfec::seek64(f,off)&&std::fread(b.data(),1,shard,f)==shard;}
inline bool check_parity_header(FILE*f,uint32_t gid,uint32_t pi,const Group&g,uint64_t shard,std::string&err){ParityHeader h;if(!zfec::seek64(f,0)||std::fread(&h,1,sizeof(h),f)!=sizeof(h)){err="short parity header";return false;}if(std::memcmp(h.magic,"OECPAR1",7)||h.version!=1||h.header_size!=sizeof(h)||h.group_id!=gid||h.parity_index!=pi||h.k!=g.k||h.m!=g.m||h.shard_size!=shard||h.stripe_count!=g.stripes||parity_header_crc(h)!=h.header_crc){err="invalid parity header";return false;}return true;}

inline bool create_group(const std::string&data_root,const std::string&parity_root,const std::string&setname,Group&g,uint64_t shard,const std::string&hash_alg,bool force,std::string&err){
  Matrix gen;if(!generator(g.k,g.m,gen)){err="cannot build RS generator";return false;}
  std::vector<FILE*> in(g.k,0),out(g.m,0);
  std::vector<Sha256> dh(g.k),ph(g.m);
  std::vector<std::vector<uint8_t> > db(g.k,std::vector<uint8_t>((size_t)shard)),pb(g.m,std::vector<uint8_t>((size_t)shard));
  g.parity.resize(g.m);g.data_crc.assign((size_t)g.stripes*g.k,0);g.parity_crc.assign((size_t)g.stripes*g.m,0);
  bool ok=false;
  do {
    for(unsigned i=0;i<g.k;++i){std::string p=join(data_root,g.data[i].rel);in[i]=zfec::fopen_utf8(p,"rb");if(!in[i]){err="cannot open data: "+p;break;}}
    bool opened=true;for(unsigned i=0;i<g.k;++i)if(!in[i])opened=false;if(!opened)break;
    if(!mkdirs(parity_root)){err="cannot create parity root";break;}
    for(unsigned p=0;p<g.m;++p){
      std::ostringstream n;n<<setname<<".g"<<std::setw(6)<<std::setfill('0')<<g.id<<".p"<<std::setw(3)<<p+1<<".oecp";g.parity[p].rel=n.str();
      std::string fp=join(parity_root,g.parity[p].rel);if(zfec::file_exists(fp)&&!force){err="parity exists (use --force): "+fp;break;}
      std::string tmp=fp+".tmp";zfec::remove_utf8(tmp);out[p]=zfec::fopen_utf8(tmp,"wb");if(!out[p]){err="cannot create parity: "+tmp;break;}
      ParityHeader h{};std::memcpy(h.magic,"OECPAR1",7);h.version=1;h.header_size=sizeof(h);h.group_id=g.id;h.parity_index=p;h.k=g.k;h.m=g.m;h.shard_size=(uint32_t)shard;h.stripe_count=g.stripes;h.payload_bytes=g.stripes*shard;h.header_crc=parity_header_crc(h);
      if(std::fwrite(&h,1,sizeof(h),out[p])!=sizeof(h)){err="cannot write parity header";break;}
    }
    bool pout=true;for(unsigned p=0;p<g.m;++p)if(!out[p])pout=false;if(!pout)break;
    for(uint64_t st=0;st<g.stripes;++st){
      uint64_t off=st*shard;
      for(unsigned i=0;i<g.k;++i){if(!read_shard(in[i],off,g.data[i].size,(size_t)shard,db[i])){err="read data failed";pout=false;break;}g.data_crc[(size_t)st*g.k+i]=zfec::crc32c(db[i].data(),db[i].size());if(hash_alg=="sha256"&&off<g.data[i].size){size_t n=(size_t)std::min<uint64_t>(shard,g.data[i].size-off);dh[i].update(db[i].data(),n);}}
      if(!pout)break;
      for(unsigned p=0;p<g.m;++p){linear(pb[p].data(),db,&gen[(g.k+p)*g.k],g.k,(size_t)shard);g.parity_crc[(size_t)st*g.m+p]=zfec::crc32c(pb[p].data(),pb[p].size());if(std::fwrite(pb[p].data(),1,pb[p].size(),out[p])!=pb[p].size()){err="write parity failed";pout=false;break;}if(hash_alg=="sha256")ph[p].update(pb[p].data(),pb[p].size());}
      if(!pout)break;
    }
    if(!pout)break;
    for(unsigned i=0;i<g.k;++i){std::fclose(in[i]);in[i]=0;if(hash_alg=="sha256")g.data[i].hash=dh[i].final();}
    for(unsigned p=0;p<g.m;++p){
      std::string finalp=join(parity_root,g.parity[p].rel),tmp=finalp+".tmp";
      if(std::fclose(out[p])!=0){out[p]=0;err="close parity failed";pout=false;break;}out[p]=0;
      g.parity[p].size=kParityHeaderSize+g.stripes*shard;
      if(hash_alg=="sha256"){std::string hh;if(!sha256_file(tmp,hh)){err="hash parity failed";pout=false;break;}g.parity[p].hash=hh;}
      zfec::remove_utf8(finalp);if(zfec::rename_utf8(tmp,finalp)!=0){err="install parity failed: "+finalp;pout=false;break;}
    }
    if(!pout)break;
    ok=true;
  } while(false);
  for(size_t i=0;i<in.size();++i)if(in[i])std::fclose(in[i]);
  for(size_t p=0;p<out.size();++p)if(out[p])std::fclose(out[p]);
  if(!ok)for(size_t p=0;p<g.parity.size();++p)if(!g.parity[p].rel.empty())zfec::remove_utf8(join(parity_root,g.parity[p].rel)+".tmp");
  return ok;
}

struct VerifySummary {uint64_t groups,stripes,bad_data,bad_parity,missing_data,missing_parity,bad_hash,unrecoverable;VerifySummary():groups(0),stripes(0),bad_data(0),bad_parity(0),missing_data(0),missing_parity(0),bad_hash(0),unrecoverable(0){}};
inline bool verify(const Manifest&m,const RootOverride&r,VerifySummary&sum,std::string&err,bool verbose){sum=VerifySummary();std::string dr=r.data_root.empty()?m.data_root:r.data_root,pr=r.parity_root.empty()?m.parity_root:r.parity_root;std::vector<uint8_t>b;for(size_t gi=0;gi<m.groups.size();++gi){const Group&g=m.groups[gi];++sum.groups;std::vector<FILE*>df(g.k,0),pf(g.m,0);std::vector<bool>dmiss(g.k),pmiss(g.m),phead(g.m);for(unsigned i=0;i<g.k;++i){std::string fp=join(dr,g.data[i].rel);df[i]=zfec::fopen_utf8(fp,"rb");dmiss[i]=!df[i];if(dmiss[i])++sum.missing_data;else{uint64_t actual=0;if(!zfec::get_file_size(fp,actual)||actual!=g.data[i].size){++sum.bad_data;if(verbose)std::fprintf(stdout,"cold: size mismatch data %s expected=%llu actual=%llu\n",g.data[i].rel.c_str(),(unsigned long long)g.data[i].size,(unsigned long long)actual);}}}for(unsigned p=0;p<g.m;++p){std::string fp=join(pr,g.parity[p].rel);pf[p]=zfec::fopen_utf8(fp,"rb");pmiss[p]=!pf[p];if(pmiss[p])++sum.missing_parity;else{uint64_t actual=0;if(!zfec::get_file_size(fp,actual)||actual!=g.parity[p].size){++sum.bad_parity;if(verbose)std::fprintf(stdout,"cold: size mismatch parity %s\n",g.parity[p].rel.c_str());}std::string e;phead[p]=check_parity_header(pf[p],g.id,p,g,m.shard_size,e);if(!phead[p]){++sum.bad_parity;if(verbose)std::fprintf(stdout,"cold: bad parity header %s\n",g.parity[p].rel.c_str());}}}
    for(uint64_t s=0;s<g.stripes;++s){++sum.stripes;unsigned bad=0,good=0;uint64_t off=s*m.shard_size;for(unsigned i=0;i<g.k;++i){bool ok=false;if(df[i]&&read_shard(df[i],off,g.data[i].size,(size_t)m.shard_size,b))ok=zfec::crc32c(b.data(),b.size())==g.data_crc[(size_t)s*g.k+i];if(!ok){++sum.bad_data;++bad;}else ++good;}for(unsigned p=0;p<g.m;++p){bool ok=false;if(pf[p]&&phead[p]&&read_parity_shard(pf[p],s,(size_t)m.shard_size,b))ok=zfec::crc32c(b.data(),b.size())==g.parity_crc[(size_t)s*g.m+p];if(!ok){++sum.bad_parity;}else ++good;}if(good<g.k){++sum.unrecoverable;if(verbose)std::fprintf(stdout,"cold: unrecoverable group=%u stripe=%llu good=%u need=%u\n",g.id,(unsigned long long)s,good,g.k);}}
    for(unsigned i=0;i<g.k;++i)if(df[i])std::fclose(df[i]);
    for(unsigned p=0;p<g.m;++p)if(pf[p])std::fclose(pf[p]);
    if(m.hash_alg=="sha256"){
      for(unsigned i=0;i<g.k;++i)if(!g.data[i].hash.empty()&&zfec::file_exists(join(dr,g.data[i].rel))){std::string h;if(!sha256_file(join(dr,g.data[i].rel),h)||h!=g.data[i].hash){++sum.bad_hash;if(verbose)std::fprintf(stdout,"cold: SHA-256 mismatch data %s\n",g.data[i].rel.c_str());}}
      for(unsigned p=0;p<g.m;++p)if(!g.parity[p].hash.empty()&&zfec::file_exists(join(pr,g.parity[p].rel))){std::string h;if(!sha256_file(join(pr,g.parity[p].rel),h)||h!=g.parity[p].hash){++sum.bad_hash;if(verbose)std::fprintf(stdout,"cold: SHA-256 mismatch parity %s\n",g.parity[p].rel.c_str());}}
    }
  }
  if(sum.unrecoverable){err="cold set has unrecoverable stripes";return false;}
  if(sum.bad_hash&&sum.bad_data==0&&sum.bad_parity==0&&sum.missing_data==0&&sum.missing_parity==0){err="whole-file SHA-256 mismatch with no localized CRC32C failure";return false;}
  return sum.bad_data==0&&sum.bad_parity==0&&sum.missing_data==0&&sum.missing_parity==0&&sum.bad_hash==0;}

inline bool build_groups(const std::vector<FileRec>&files,uint32_t maxk,uint32_t m,uint64_t shard,const std::string&grouping,std::vector<Group>&groups){std::vector<FileRec>x=files;if(grouping=="size")std::stable_sort(x.begin(),x.end(),[](const FileRec&a,const FileRec&b){return a.size>b.size;});size_t ng=(x.size()+maxk-1)/maxk;if(!ng)ng=1;size_t base=x.size()/ng,extra=x.size()%ng,at=0;for(size_t g=0;g<ng;++g){size_t cnt=base+(g<extra?1:0);Group q;q.id=(uint32_t)(g+1);q.k=(uint32_t)cnt;q.m=m;q.data.assign(x.begin()+at,x.begin()+at+cnt);uint64_t mx=0;for(size_t i=0;i<cnt;++i)mx=std::max(mx,q.data[i].size);q.stripes=ceil_div(mx,shard);if(!q.stripes)q.stripes=1;groups.push_back(q);at+=cnt;}return true;}

inline bool seal(const std::vector<std::string>&data_paths,const std::string&data_root,const std::string&setname,SealOptions opt,std::string&manifest_path,std::string&err){if(!valid_options(opt,err))return false;if(data_paths.empty()){err="no data parts";return false;}if(opt.profile=="safe"&&opt.k==20&&opt.m==2){opt.k=20;opt.m=3;}else if(opt.profile=="space"&&opt.k==20&&opt.m==2){opt.k=32;opt.m=2;}std::vector<FileRec>fr;for(size_t i=0;i<data_paths.size();++i){uint64_t sz=0;if(!zfec::get_file_size(data_paths[i],sz)){err="cannot stat "+data_paths[i];return false;}FileRec r;r.rel=basename(data_paths[i]);r.size=sz;fr.push_back(r);}Manifest man;man.version=1;man.max_k=opt.k;man.m=opt.m;man.shard_size=opt.shard;man.profile=opt.profile;man.grouping=opt.grouping;man.hash_alg=opt.hash_alg;man.data_root=data_root;man.set_name=setname;man.manifest_copies=opt.copies;man.parity_root=opt.output.empty()?join(data_root,setname+".oecp"):opt.output;if(!mkdirs(man.parity_root)){err="cannot create parity root: "+man.parity_root;return false;}build_groups(fr,opt.k,opt.m,opt.shard,opt.grouping,man.groups);for(size_t i=0;i<man.groups.size();++i){std::fprintf(stdout,"oec_cold: seal group %u/%llu k=%u m=%u stripes=%llu\n",man.groups[i].id,(unsigned long long)man.groups.size(),man.groups[i].k,man.groups[i].m,(unsigned long long)man.groups[i].stripes);if(!create_group(man.data_root,man.parity_root,setname,man.groups[i],opt.shard,opt.hash_alg,opt.force,err))return false;}manifest_path=opt.manifest.empty()?join(man.parity_root,setname+".oecmanifest"):opt.manifest;if(zfec::file_exists(manifest_path)&&!opt.force){err="manifest exists (use --force): "+manifest_path;return false;}std::string txt=manifest_serialize(man);if(!write_atomic(manifest_path,txt,err))return false;std::string cd=opt.copy_dir.empty()?dirname(manifest_path):opt.copy_dir;if(!mkdirs(cd)){err="cannot create manifest replica dir";return false;}for(unsigned i=1;i<=opt.copies;++i){std::ostringstream p;p<<join(cd,basename(manifest_path))<<".copy"<<i;if(!write_atomic(p.str(),txt,err))return false;}return true;}

inline bool precheck_group(const Manifest&m,const Group&g,const std::string&dr,const std::string&pr,std::vector<bool>&lane_bad,std::vector<bool>&par_bad,uint64_t&unrec,std::string&err){lane_bad.assign(g.k,false);par_bad.assign(g.m,false);unrec=0;std::vector<FILE*>df(g.k,0),pf(g.m,0);std::vector<bool>ph(g.m,false);for(unsigned i=0;i<g.k;++i){std::string fp=join(dr,g.data[i].rel);df[i]=zfec::fopen_utf8(fp,"rb");uint64_t actual=0;if(!df[i]||!zfec::get_file_size(fp,actual)||actual!=g.data[i].size)lane_bad[i]=true;}for(unsigned p=0;p<g.m;++p){std::string fp=join(pr,g.parity[p].rel);pf[p]=zfec::fopen_utf8(fp,"rb");uint64_t actual=0;if(pf[p]){std::string e;ph[p]=check_parity_header(pf[p],g.id,p,g,m.shard_size,e);if(!ph[p]||!zfec::get_file_size(fp,actual)||actual!=g.parity[p].size)par_bad[p]=true;}else par_bad[p]=true;}std::vector<uint8_t>b;for(uint64_t s=0;s<g.stripes;++s){unsigned good=0;uint64_t off=s*m.shard_size;for(unsigned i=0;i<g.k;++i){bool ok=df[i]&&read_shard(df[i],off,g.data[i].size,(size_t)m.shard_size,b)&&zfec::crc32c(b.data(),b.size())==g.data_crc[(size_t)s*g.k+i];if(ok)++good;else lane_bad[i]=true;}for(unsigned p=0;p<g.m;++p){bool ok=pf[p]&&ph[p]&&read_parity_shard(pf[p],s,(size_t)m.shard_size,b)&&zfec::crc32c(b.data(),b.size())==g.parity_crc[(size_t)s*g.m+p];if(ok)++good;else par_bad[p]=true;}if(good<g.k)++unrec;}for(unsigned i=0;i<g.k;++i)if(df[i])std::fclose(df[i]);for(unsigned p=0;p<g.m;++p)if(pf[p])std::fclose(pf[p]);if(unrec){err="group has unrecoverable stripes";return false;}return true;}

inline bool repair_group_data(const Manifest&m,const Group&g,const std::string&dr,const std::string&pr,const std::vector<bool>&lane_bad,bool install,std::string&err){bool any=false;for(size_t i=0;i<lane_bad.size();++i)any|=lane_bad[i];if(!any)return true;Matrix gen;if(!generator(g.k,g.m,gen)){err="RS generator failure";return false;}std::vector<FILE*>df(g.k,0),pf(g.m,0),out(g.k,0);std::vector<bool>ph(g.m,false);for(unsigned i=0;i<g.k;++i){df[i]=zfec::fopen_utf8(join(dr,g.data[i].rel),"rb");if(lane_bad[i]){std::string dst=join(dr,g.data[i].rel)+(install?".oec-cold-repair.tmp":".repaired");zfec::remove_utf8(dst);out[i]=zfec::fopen_utf8(dst,"wb");if(!out[i]){err="cannot create repair output: "+dst;goto fail;}}}for(unsigned p=0;p<g.m;++p){pf[p]=zfec::fopen_utf8(join(pr,g.parity[p].rel),"rb");if(pf[p]){std::string e;ph[p]=check_parity_header(pf[p],g.id,p,g,m.shard_size,e);}}
  {std::vector<std::vector<uint8_t> >rows(g.k+g.m,std::vector<uint8_t>((size_t)m.shard_size)),selected;std::vector<uint8_t>ok(g.k+g.m);for(uint64_t s=0;s<g.stripes;++s){uint64_t off=s*m.shard_size;for(unsigned i=0;i<g.k;++i)ok[i]=df[i]&&read_shard(df[i],off,g.data[i].size,(size_t)m.shard_size,rows[i])&&zfec::crc32c(rows[i].data(),rows[i].size())==g.data_crc[(size_t)s*g.k+i];for(unsigned p=0;p<g.m;++p)ok[g.k+p]=pf[p]&&ph[p]&&read_parity_shard(pf[p],s,(size_t)m.shard_size,rows[g.k+p])&&zfec::crc32c(rows[g.k+p].data(),rows[g.k+p].size())==g.parity_crc[(size_t)s*g.m+p];std::vector<unsigned>pick;for(unsigned r=0;r<g.k+g.m&&pick.size()<g.k;++r)if(ok[r])pick.push_back(r);if(pick.size()<g.k){err="unrecoverable stripe during repair";goto fail;}Matrix a(g.k*g.k);selected.resize(g.k);for(unsigned r=0;r<g.k;++r){selected[r]=rows[pick[r]];for(unsigned c=0;c<g.k;++c)a[r*g.k+c]=gen[pick[r]*g.k+c];}Matrix inv;if(!mat_inv(a,g.k,inv)){err="RS decode matrix singular";goto fail;}for(unsigned i=0;i<g.k;++i)if(lane_bad[i]){std::vector<uint8_t>rec((size_t)m.shard_size);if(ok[i])rec=rows[i];else linear(rec.data(),selected,&inv[i*g.k],g.k,(size_t)m.shard_size);size_t n=(size_t)((off>=g.data[i].size)?0:std::min<uint64_t>(m.shard_size,g.data[i].size-off));if(n&&std::fwrite(rec.data(),1,n,out[i])!=n){err="repair write failed";goto fail;}}}}
  for(unsigned i=0;i<g.k;++i){if(df[i]){std::fclose(df[i]);df[i]=0;}if(out[i]){if(std::fclose(out[i])!=0){out[i]=0;err="repair close failed";goto fail;}out[i]=0;}}for(unsigned p=0;p<g.m;++p)if(pf[p]){std::fclose(pf[p]);pf[p]=0;}
  for(unsigned i=0;i<g.k;++i)if(lane_bad[i]&&install){std::string orig=join(dr,g.data[i].rel),tmp=orig+".oec-cold-repair.tmp",bad=orig+".oec-bad";unsigned n=0;while(zfec::file_exists(bad)){std::ostringstream b;b<<orig<<".oec-bad."<<++n;bad=b.str();}if(zfec::file_exists(orig)&&zfec::rename_utf8(orig,bad)!=0){err="cannot preserve damaged file: "+orig;return false;}if(zfec::rename_utf8(tmp,orig)!=0){if(zfec::file_exists(bad))zfec::rename_utf8(bad,orig);err="cannot install repaired file: "+orig;return false;}}
  return true;
fail:for(unsigned i=0;i<g.k;++i){if(df[i])std::fclose(df[i]);if(out[i])std::fclose(out[i]);}for(unsigned p=0;p<g.m;++p)if(pf[p])std::fclose(pf[p]);return false;}

inline bool repair(Manifest&m,const RootOverride&r,std::string&err){
  std::string dr=r.data_root.empty()?m.data_root:r.data_root,pr=r.parity_root.empty()?m.parity_root:r.parity_root;
  for(size_t gi=0;gi<m.groups.size();++gi){
    std::vector<bool>db,pb;uint64_t un=0;
    if(!precheck_group(m,m.groups[gi],dr,pr,db,pb,un,err))return false;
    bool anyd=false,anyp=false;for(size_t i=0;i<db.size();++i)anyd|=db[i];for(size_t p=0;p<pb.size();++p)anyp|=pb[p];
    if(anyd){std::fprintf(stdout,"oec_cold: repair data group %u\n",m.groups[gi].id);if(!repair_group_data(m,m.groups[gi],dr,pr,db,!r.no_install,err))return false;}
    if(anyp&&!r.no_install){std::fprintf(stdout,"oec_cold: regenerate parity group %u\n",m.groups[gi].id);if(!create_group(dr,pr,m.set_name,m.groups[gi],m.shard_size,m.hash_alg,true,err))return false;}
  }
  return true;
}

inline std::string human(uint64_t n){const char*u[] = {"B","KiB","MiB","GiB","TiB"};double x=(double)n;int i=0;while(x>=1024&&i<4){x/=1024;++i;}std::ostringstream o;o<<std::fixed<<std::setprecision(i?2:0)<<x<<" "<<u[i];return o.str();}
inline void info(const Manifest&m,const std::string&used){uint64_t data=0,par=0;for(size_t g=0;g<m.groups.size();++g){for(size_t i=0;i<m.groups[g].data.size();++i)data+=m.groups[g].data[i].size;for(size_t p=0;p<m.groups[g].parity.size();++p)par+=m.groups[g].parity[p].size;}std::fprintf(stdout,"OECOLD1 manifest=%s\nprofile=%s max_k=%u m=%u shard=%s grouping=%s hash=%s\ngroups=%llu data=%s parity=%s actual_overhead=%.2f%%\ndata_root=%s\nparity_root=%s\n",used.c_str(),m.profile.c_str(),m.max_k,m.m,human(m.shard_size).c_str(),m.grouping.c_str(),m.hash_alg.c_str(),(unsigned long long)m.groups.size(),human(data).c_str(),human(par).c_str(),data?100.0*(double)par/(double)data:0.0,m.data_root.c_str(),m.parity_root.c_str());}

} // namespace oeccold
