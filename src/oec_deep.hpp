#pragma once
// oec_deep.hpp - deep IDX2 dedup backend injected after the native HTIndex declaration.
// Requires upstream HT, HTIndex and vector<HT> declarations to already be visible.
#include "oec_idx.hpp"
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace oecdeep {
static const uint32_t kDeepVersion=1;
static const uint32_t kSlotEmpty=0, kSlotUsed=1;
#pragma pack(push,1)
struct DeepMeta {
  char magic[8]; uint32_t version,header_size;
  uint64_t capacity, committed_generation, last_generation, indexed_through, committed_count;
  uint32_t slot_size, meta_crc32c; uint64_t reserved[4];
};
struct FragmentSlot {
  unsigned char sha1[20]; uint32_t fragment_id; int64_t usize;
  uint64_t generation; uint32_t state, crc32c;
};
#pragma pack(pop)
inline uint32_t meta_crc(DeepMeta m){m.meta_crc32c=0;return zfec::crc32c(&m,sizeof(m));}
inline uint32_t slot_crc(FragmentSlot s){s.crc32c=0;return zfec::crc32c(&s,sizeof(s));}
inline uint64_t sha_key(const void* p){const unsigned char* s=(const unsigned char*)p;uint64_t h=1469598103934665603ULL;for(int i=0;i<20;++i){h^=s[i];h*=1099511628211ULL;}return h;}
inline uint64_t next_pow2(uint64_t x){uint64_t n=16;while(n<x && n<(1ULL<<62))n<<=1;return n;}
inline std::string envs(const char*n){const char*p=std::getenv(n);return p?std::string(p):std::string();}
inline uint64_t parse_mem_budget(){std::string s=envs("ZPAQOEC_IDX_MEMORY");if(s.empty()||s=="auto")return 64ULL<<20;if(s=="0")return 0;char*e=0;double v=std::strtod(s.c_str(),&e);if(e==s.c_str()||v<0)return 64ULL<<20;uint64_t m=1;if(*e=='G'||*e=='g')m=1ULL<<30;else if(*e=='M'||*e=='m')m=1ULL<<20;else if(*e=='K'||*e=='k')m=1ULL<<10;return (uint64_t)(v*m);}

class RWMap { public: RWMap():p_(0),n_(0)
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
,f_(INVALID_HANDLE_VALUE),m_(0)
#else
,fd_(-1)
#endif
{} ~RWMap(){close();}
bool open(const std::string&path,std::string&err){close();
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
 int wn=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,0,0);if(wn<=0){err="deep idx path conversion failed";return false;}std::vector<wchar_t>w((size_t)wn);MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,w.data(),wn);f_=CreateFileW(w.data(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);if(f_==INVALID_HANDLE_VALUE){err="cannot open deep idx";return false;}LARGE_INTEGER z;if(!GetFileSizeEx(f_,&z)||z.QuadPart<=0){err="cannot size deep idx";close();return false;}n_=(size_t)z.QuadPart;m_=CreateFileMappingW(f_,0,PAGE_READWRITE,0,0,0);if(!m_){err="deep CreateFileMapping failed";close();return false;}p_=(unsigned char*)MapViewOfFile(m_,FILE_MAP_READ|FILE_MAP_WRITE,0,0,0);if(!p_){err="deep MapViewOfFile failed";close();return false;}
#else
 fd_=::open(path.c_str(),O_RDWR);if(fd_<0){err="cannot open deep idx";return false;}struct stat st;if(fstat(fd_,&st)!=0||st.st_size<=0){err="cannot size deep idx";close();return false;}n_=(size_t)st.st_size;void*q=mmap(0,n_,PROT_READ|PROT_WRITE,MAP_SHARED,fd_,0);if(q==MAP_FAILED){err="deep mmap failed";close();return false;}p_=(unsigned char*)q;
#endif
 return true;}
void flush(size_t off=0,size_t len=0){if(!p_)return;if(!len)len=n_-std::min(off,n_);
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
 FlushViewOfFile(p_+off,len);FlushFileBuffers(f_);
#else
 msync(p_+off,len,MS_SYNC);
#endif
}
void close(){
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
 if(p_)UnmapViewOfFile(p_);if(m_)CloseHandle(m_);if(f_!=INVALID_HANDLE_VALUE)CloseHandle(f_);p_=0;m_=0;f_=INVALID_HANDLE_VALUE;n_=0;
#else
 if(p_)munmap(p_,n_);if(fd_>=0)::close(fd_);p_=0;n_=0;fd_=-1;
#endif
} unsigned char*data(){return p_;}size_t size()const{return n_;}
private:unsigned char*p_;size_t n_;
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
 HANDLE f_,m_;
#else
 int fd_;
#endif
};

inline bool resize_file(const std::string&path,uint64_t n,std::string&err){
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
 FILE*f=zfec::fopen_utf8(path,"r+b");if(!f){err="cannot open idx for deep resize";return false;}int fd=_fileno(f);int rc=_chsize_s(fd,n);std::fclose(f);if(rc){err="cannot resize idx for deep table";return false;}return true;
#else
 if(::truncate(path.c_str(),(off_t)n)!=0){err="cannot resize idx for deep table";return false;}return true;
#endif
}
inline bool read_h2(const std::string&path,oecidx::HeaderV2&h,std::string&err){FILE*f=zfec::fopen_utf8(path,"rb");if(!f){err="cannot open idx header";return false;}bool ok=std::fread(&h,1,sizeof(h),f)==sizeof(h);std::fclose(f);if(!ok||std::memcmp(h.magic,"OECIDX2",7)!=0||h.version!=2){err="deep backend requires OECIDX2";return false;}return true;}
inline bool write_h2(const std::string&path,oecidx::HeaderV2 h,std::string&err){h.header_crc32c=oecidx::header_crc_v2(h);FILE*f=zfec::fopen_utf8(path,"r+b");if(!f){err="cannot update idx header";return false;}bool ok=std::fwrite(&h,1,sizeof(h),f)==sizeof(h)&&std::fflush(f)==0;std::fclose(f);if(!ok)err="cannot write idx header";return ok;}

template<class HTVec> bool create_table(const std::string&path,const HTVec&ht,uint64_t expected,std::string&err){oecidx::HeaderV2 h;if(!read_h2(path,h,err))return false;uint64_t cap=next_pow2(std::max<uint64_t>(expected*2+16,ht.size()*2+16));uint64_t off=std::max<uint64_t>(h.path_hash_offset+h.path_hash_count*h.path_hash_record_size,h.info_offset+h.info_size);off=std::max<uint64_t>(off,h.file_offset+h.file_count*h.file_record_size);off=std::max<uint64_t>(off,h.string_offset+h.string_size);off=(off+63)&~63ULL;uint64_t total=off+sizeof(DeepMeta)+cap*sizeof(FragmentSlot);if(!resize_file(path,total,err))return false;RWMap m;if(!m.open(path,err))return false;DeepMeta*dm=(DeepMeta*)(m.data()+off);std::memset(dm,0,sizeof(*dm));std::memcpy(dm->magic,"OECFRG2",7);dm->version=kDeepVersion;dm->header_size=sizeof(*dm);dm->capacity=cap;dm->committed_generation=1;dm->last_generation=1;dm->indexed_through=ht.size();dm->slot_size=sizeof(FragmentSlot);FragmentSlot*slots=(FragmentSlot*)(dm+1);std::memset(slots,0,(size_t)(cap*sizeof(FragmentSlot)));uint64_t cnt=0;for(uint64_t id=1;id<ht.size();++id){const unsigned char*sha=(const unsigned char*)ht[(size_t)id].sha1;uint64_t pos=sha_key(sha)&(cap-1);for(uint64_t k=0;k<cap;++k){FragmentSlot&s=slots[(pos+k)&(cap-1)];if(s.state==kSlotEmpty){std::memcpy(s.sha1,sha,20);s.fragment_id=(uint32_t)id;s.usize=ht[(size_t)id].usize;s.generation=1;s.state=kSlotUsed;s.crc32c=slot_crc(s);++cnt;break;}}}dm->committed_count=cnt;dm->meta_crc32c=meta_crc(*dm);m.flush();m.close();h.flags|=(1u<<4)|(1u<<5);h.fragment_offset=off;h.fragment_count=cap;h.fragment_record_size=sizeof(FragmentSlot);h.fragment_crc32c=0;return write_h2(path,h,err);}

struct Hot { unsigned char sha1[20]; uint32_t id; uint8_t used; Hot():id(0),used(0){std::memset(sha1,0,20);} };
}

class OecHybridHTIndex {
 public:
  OecHybridHTIndex(std::vector<HT>&ht,unsigned expected):ht_(ht),fallback_(0),dm_(0),slots_(0),active_(0),deep_(false),hits_(0),miss_(0){
    const std::string path=oecdeep::envs("ZPAQOEC_DEEP_IDX");
    if(path.empty()){fallback_=new HTIndex(ht,expected);return;}
    std::string err;oecidx::HeaderV2 h;
    if(!oecdeep::read_h2(path,h,err)){fallback_=new HTIndex(ht,expected);return;}
    bool need=!(h.flags&(1u<<4))||!h.fragment_offset||h.fragment_record_size!=sizeof(oecdeep::FragmentSlot)||h.fragment_count<expected*2ULL;
    if(need && !oecdeep::create_table(path,ht,expected,err)){std::fprintf(stderr,"oec deep: fallback RAM (%s)\n",err.c_str());fallback_=new HTIndex(ht,expected);return;}
    if(!map_.open(path,err)){fallback_=new HTIndex(ht,expected);return;}
    oecidx::HeaderV2*ph=(oecidx::HeaderV2*)map_.data();if(ph->fragment_offset+sizeof(oecdeep::DeepMeta)+ph->fragment_count*sizeof(oecdeep::FragmentSlot)>map_.size()){map_.close();fallback_=new HTIndex(ht,expected);return;}
    dm_=(oecdeep::DeepMeta*)(map_.data()+ph->fragment_offset);slots_=(oecdeep::FragmentSlot*)(dm_+1);if(std::memcmp(dm_->magic,"OECFRG2",7)!=0||dm_->meta_crc32c!=oecdeep::meta_crc(*dm_)||dm_->capacity!=ph->fragment_count){map_.close();fallback_=new HTIndex(ht,expected);return;}
    // Catch up authoritative HT records not indexed yet. Old uncommitted generations are reusable tombstones.
    if(dm_->indexed_through>ht.size()){map_.close();fallback_=new HTIndex(ht,expected);return;}
    for(uint64_t id=dm_->indexed_through;id<ht.size();++id) insert_raw((const unsigned char*)ht[(size_t)id].sha1,(uint32_t)id,ht[(size_t)id].usize,dm_->committed_generation);
    dm_->indexed_through=ht.size();active_=dm_->last_generation+1;dm_->last_generation=active_;dm_->meta_crc32c=oecdeep::meta_crc(*dm_);map_.flush();deep_=true;
    uint64_t budget=oecdeep::parse_mem_budget();uint64_t n=budget/sizeof(oecdeep::Hot);if(n>1u<<24)n=1u<<24;if(n){uint64_t p=1;while((p<<1)<=n)p<<=1;hot_.resize((size_t)p);}std::fprintf(stdout,"oec deep: IDX2 dedup enabled slots=%llu hot=%lluMB indexed=%llu\n",(unsigned long long)dm_->capacity,(unsigned long long)(hot_.size()*sizeof(oecdeep::Hot)/(1<<20)),(unsigned long long)dm_->indexed_through);
  }
  ~OecHybridHTIndex(){if(fallback_)delete fallback_;}
  unsigned find(const char*sha){if(fallback_)return fallback_->find(sha);const unsigned char*s=(const unsigned char*)sha;uint64_t key=oecdeep::sha_key(s);if(!hot_.empty()){oecdeep::Hot&h=hot_[(size_t)(key&(hot_.size()-1))];if(h.used&&std::memcmp(h.sha1,s,20)==0&&h.id<ht_.size()&&std::memcmp(ht_[h.id].sha1,s,20)==0){++hits_;return h.id;}}
    uint64_t pos=key&(dm_->capacity-1);for(uint64_t k=0;k<dm_->capacity;++k){oecdeep::FragmentSlot&r=slots_[(pos+k)&(dm_->capacity-1)];if(r.state==oecdeep::kSlotEmpty){++miss_;return 0;}bool visible=r.generation<=dm_->committed_generation||r.generation==active_;if(visible&&r.crc32c==oecdeep::slot_crc(r)&&std::memcmp(r.sha1,s,20)==0&&r.fragment_id<ht_.size()&&std::memcmp(ht_[r.fragment_id].sha1,s,20)==0&&ht_[r.fragment_id].usize==r.usize){hot_put(key,s,r.fragment_id);++hits_;return r.fragment_id;}}
    ++miss_;return 0;}
  void update(){if(fallback_){fallback_->update();return;}if(ht_.size()<2)return;uint32_t id=(uint32_t)(ht_.size()-1);insert_raw((const unsigned char*)ht_[id].sha1,id,ht_[id].usize,active_);hot_put(oecdeep::sha_key(ht_[id].sha1),(const unsigned char*)ht_[id].sha1,id);}
  void commit(){if(fallback_||!deep_)return;dm_->indexed_through=ht_.size();dm_->committed_generation=active_;dm_->meta_crc32c=oecdeep::meta_crc(*dm_);map_.flush();std::fprintf(stdout,"oec deep: committed generation=%llu indexed=%llu hits=%llu misses=%llu\n",(unsigned long long)active_,(unsigned long long)dm_->indexed_through,(unsigned long long)hits_,(unsigned long long)miss_);}
 private:
  void hot_put(uint64_t key,const unsigned char*s,uint32_t id){if(hot_.empty())return;oecdeep::Hot&h=hot_[(size_t)(key&(hot_.size()-1))];std::memcpy(h.sha1,s,20);h.id=id;h.used=1;}
  void insert_raw(const unsigned char*s,uint32_t id,int64_t usize,uint64_t gen){uint64_t pos=oecdeep::sha_key(s)&(dm_->capacity-1);uint64_t reusable=UINT64_MAX;for(uint64_t k=0;k<dm_->capacity;++k){uint64_t ix=(pos+k)&(dm_->capacity-1);oecdeep::FragmentSlot&r=slots_[ix];if(r.state==oecdeep::kSlotEmpty){if(reusable!=UINT64_MAX)ix=reusable;oecdeep::FragmentSlot&d=slots_[ix];std::memset(&d,0,sizeof(d));std::memcpy(d.sha1,s,20);d.fragment_id=id;d.usize=usize;d.generation=gen;d.state=oecdeep::kSlotUsed;d.crc32c=oecdeep::slot_crc(d);return;}if(r.generation>dm_->committed_generation&&r.generation!=active_&&reusable==UINT64_MAX)reusable=ix;if(std::memcmp(r.sha1,s,20)==0&&(r.generation<=dm_->committed_generation||r.generation==active_))return;}if(reusable!=UINT64_MAX){oecdeep::FragmentSlot&d=slots_[reusable];std::memset(&d,0,sizeof(d));std::memcpy(d.sha1,s,20);d.fragment_id=id;d.usize=usize;d.generation=gen;d.state=oecdeep::kSlotUsed;d.crc32c=oecdeep::slot_crc(d);}}
  std::vector<HT>&ht_;HTIndex*fallback_;oecdeep::RWMap map_;oecdeep::DeepMeta*dm_;oecdeep::FragmentSlot*slots_;uint64_t active_;bool deep_;std::vector<oecdeep::Hot>hot_;uint64_t hits_,miss_;
};
