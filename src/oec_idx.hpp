#pragma once
// oec_idx.hpp - disposable mmap-backed OEC acceleration cache.
// OECIDX2 adds structured file metadata + path hash lookup while retaining
// materialized LIST/INFO views for exact upstream-compatible presentation.
// The .000 ZPAQ index remains authoritative; .idx is always rebuildable.

#include "zfec.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <sstream>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  #include <windows.h>
  #include <sys/stat.h>
#else
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <sys/mman.h>
  #include <fcntl.h>
  #include <unistd.h>
#endif

namespace oecidx {
static const uint32_t kVersion1=1, kVersion2=2, kCurrentVersion=2;
static const uint32_t kFlagList=1u<<0, kFlagInfo=1u<<1, kFlagFiles=1u<<2, kFlagPathHash=1u<<3;
static const size_t kSampleBytes=64u*1024u;

#pragma pack(push,1)
struct HeaderV1 {
  char magic[8]; uint32_t version,header_size,flags,reserved0;
  uint64_t source_size; int64_t source_mtime;
  uint32_t source_crc_first,source_crc_middle,source_crc_last,reserved1;
  uint64_t list_offset,list_size; uint32_t list_crc32c,reserved2;
  uint64_t info_offset,info_size; uint32_t info_crc32c,reserved3;
  uint64_t created_unix; uint32_t header_crc32c,reserved4;
};
struct HeaderV2 {
  char magic[8]; uint32_t version,header_size,flags,reserved0;
  uint64_t source_size; int64_t source_mtime;
  uint32_t source_crc_first,source_crc_middle,source_crc_last,reserved1;
  uint64_t list_offset,list_size; uint32_t list_crc32c,reserved2;
  uint64_t info_offset,info_size; uint32_t info_crc32c,reserved3;
  uint64_t file_offset,file_count; uint32_t file_record_size,file_crc32c;
  uint64_t string_offset,string_size; uint32_t string_crc32c,reserved4;
  uint64_t path_hash_offset,path_hash_count; uint32_t path_hash_record_size,path_hash_crc32c;
  uint64_t fragment_offset,fragment_count; uint32_t fragment_record_size,fragment_crc32c; // reserved until deep Jidac export
  uint64_t block_offset,block_count; uint32_t block_record_size,block_crc32c;             // reserved until deep Jidac export
  uint64_t created_unix; uint32_t header_crc32c,reserved5;
};
struct FileRecord {
  uint64_t path_offset; uint32_t path_size; uint32_t modified_offset;
  uint32_t modified_size; uint32_t attributes_offset; uint32_t attributes_size;
  uint64_t size; uint64_t version; int32_t ratio_percent;
  uint8_t type; uint8_t status; uint16_t reserved;
};
struct PathHashRecord { uint64_t hash; uint64_t file_index; };
#pragma pack(pop)

typedef HeaderV1 Header; // source compatibility for older extension call sites
struct FileInput {
  std::string path,modified,attributes; uint64_t size,version; int ratio_percent; char status; uint8_t type;
  FileInput():size(0),version(0),ratio_percent(-1),status('+'),type(0){}
};
struct Fingerprint { uint64_t size; int64_t mtime; uint32_t first,middle,last; Fingerprint():size(0),mtime(0),first(0),middle(0),last(0){} };

inline uint32_t header_crc_v1(HeaderV1 h){h.header_crc32c=0;return zfec::crc32c(&h,sizeof(h));}
inline uint32_t header_crc_v2(HeaderV2 h){h.header_crc32c=0;return zfec::crc32c(&h,sizeof(h));}
inline uint64_t hash_path(const std::string& s){uint64_t h=1469598103934665603ULL;for(size_t i=0;i<s.size();++i){unsigned char c=(unsigned char)s[i];if(c=='\\')c='/';h^=c;h*=1099511628211ULL;}return h;}
inline int64_t file_mtime(const std::string& p){
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
 struct _stat64 st;if(zfec::stat64_utf8(p,&st)!=0)return -1;return (int64_t)st.st_mtime;
#else
 struct stat st;if(stat(p.c_str(),&st)!=0)return -1;return (int64_t)st.st_mtime;
#endif
}
inline bool sample_crc(FILE* f,uint64_t off,uint64_t source_size,uint32_t& out){if(!source_size){out=0;return true;}if(off>=source_size)off=source_size-1;uint64_t n=std::min<uint64_t>(kSampleBytes,source_size-off);std::vector<uint8_t>b((size_t)n);if(!zfec::seek64(f,off))return false;size_t got=std::fread(b.data(),1,b.size(),f);if(got!=b.size())return false;out=zfec::crc32c(b.data(),b.size());return true;}
inline bool fingerprint(const std::string& source,Fingerprint& fp,std::string& err){if(!zfec::get_file_size(source,fp.size)){err="cannot stat OEC zero-part index: "+source;return false;}fp.mtime=file_mtime(source);FILE*f=zfec::fopen_utf8(source,"rb");if(!f){err="cannot open OEC zero-part index: "+source;return false;}uint64_t mid=fp.size>kSampleBytes?fp.size/2:0,last=fp.size>kSampleBytes?fp.size-kSampleBytes:0;bool ok=sample_crc(f,0,fp.size,fp.first)&&sample_crc(f,mid,fp.size,fp.middle)&&sample_crc(f,last,fp.size,fp.last);std::fclose(f);if(!ok){err="cannot fingerprint OEC zero-part index: "+source;return false;}return true;}
inline bool same_fingerprint(const HeaderV1&h,const Fingerprint&f){return h.source_size==f.size&&h.source_mtime==f.mtime&&h.source_crc_first==f.first&&h.source_crc_middle==f.middle&&h.source_crc_last==f.last;}
inline bool same_fingerprint(const HeaderV2&h,const Fingerprint&f){return h.source_size==f.size&&h.source_mtime==f.mtime&&h.source_crc_first==f.first&&h.source_crc_middle==f.middle&&h.source_crc_last==f.last;}
inline bool atomic_replace(const std::string&tmp,const std::string&dst,std::string&err){const std::string bak=dst+".oecidx.bak";bool had=zfec::file_exists(dst);if(had){zfec::remove_utf8(bak);if(zfec::rename_utf8(dst,bak)!=0){err="cannot move old idx aside: "+dst;return false;}}if(zfec::rename_utf8(tmp,dst)!=0){if(had)zfec::rename_utf8(bak,dst);err="cannot install idx: "+dst;return false;}if(had)zfec::remove_utf8(bak);return true;}

inline uint32_t append_string(std::string&pool,const std::string&s){uint32_t o=(uint32_t)pool.size();pool.append(s);return o;}
inline bool write_cache_v2(const std::string&idx_path,const std::string&source,const std::string&list_text,const std::string&info_text,const std::vector<FileInput>&inputs,std::string&err){
 Fingerprint fp;if(!fingerprint(source,fp,err))return false;
 // Preserve an existing mutable deep fragment section across metadata refreshes.
 // The section is append-only-at-EOF by oec_deep.hpp, so it can be copied
 // opaquely without coupling this base cache writer to the deep record layout.
 std::vector<uint8_t> deepraw; uint64_t old_frag_count=0; uint32_t old_frag_rec=0, old_deep_flags=0;
 if(zfec::file_exists(idx_path)){FILE*of=zfec::fopen_utf8(idx_path,"rb");if(of){HeaderV2 oh;uint64_t osz=0;zfec::get_file_size(idx_path,osz);if(std::fread(&oh,1,sizeof(oh),of)==sizeof(oh)&&std::memcmp(oh.magic,"OECIDX2",7)==0&&oh.version==2&&oh.fragment_offset&&oh.fragment_offset<osz){uint64_t dn=osz-oh.fragment_offset;if(dn<(1ULL<<34)){deepraw.resize((size_t)dn);if(!zfec::seek64(of,oh.fragment_offset)||std::fread(deepraw.data(),1,deepraw.size(),of)!=deepraw.size())deepraw.clear();else{old_frag_count=oh.fragment_count;old_frag_rec=oh.fragment_record_size;old_deep_flags=oh.flags&((1u<<4)|(1u<<5));}}}std::fclose(of);}}
 std::vector<FileRecord> files;std::vector<PathHashRecord> hashes;std::string pool;files.reserve(inputs.size());hashes.reserve(inputs.size());
 for(size_t i=0;i<inputs.size();++i){FileRecord r;std::memset(&r,0,sizeof(r));r.path_offset=append_string(pool,inputs[i].path);r.path_size=(uint32_t)inputs[i].path.size();r.modified_offset=append_string(pool,inputs[i].modified);r.modified_size=(uint32_t)inputs[i].modified.size();r.attributes_offset=append_string(pool,inputs[i].attributes);r.attributes_size=(uint32_t)inputs[i].attributes.size();r.size=inputs[i].size;r.version=inputs[i].version;r.ratio_percent=inputs[i].ratio_percent;r.type=inputs[i].type;r.status=(uint8_t)inputs[i].status;files.push_back(r);PathHashRecord ph;ph.hash=hash_path(inputs[i].path);ph.file_index=(uint64_t)i;hashes.push_back(ph);}
 std::sort(hashes.begin(),hashes.end(),[](const PathHashRecord&a,const PathHashRecord&b){return a.hash<b.hash||(a.hash==b.hash&&a.file_index<b.file_index);});
 HeaderV2 h;std::memset(&h,0,sizeof(h));std::memcpy(h.magic,"OECIDX2",7);h.version=kVersion2;h.header_size=sizeof(h);h.flags=(list_text.empty()?0:kFlagList)|(info_text.empty()?0:kFlagInfo)|(files.empty()?0:kFlagFiles)|(hashes.empty()?0:kFlagPathHash);h.source_size=fp.size;h.source_mtime=fp.mtime;h.source_crc_first=fp.first;h.source_crc_middle=fp.middle;h.source_crc_last=fp.last;
 uint64_t off=sizeof(h);h.list_offset=off;h.list_size=list_text.size();h.list_crc32c=zfec::crc32c(list_text.data(),list_text.size());off+=h.list_size;h.info_offset=off;h.info_size=info_text.size();h.info_crc32c=zfec::crc32c(info_text.data(),info_text.size());off+=h.info_size;h.file_offset=off;h.file_count=files.size();h.file_record_size=sizeof(FileRecord);h.file_crc32c=zfec::crc32c(files.data(),files.size()*sizeof(FileRecord));off+=files.size()*sizeof(FileRecord);h.string_offset=off;h.string_size=pool.size();h.string_crc32c=zfec::crc32c(pool.data(),pool.size());off+=pool.size();h.path_hash_offset=off;h.path_hash_count=hashes.size();h.path_hash_record_size=sizeof(PathHashRecord);h.path_hash_crc32c=zfec::crc32c(hashes.data(),hashes.size()*sizeof(PathHashRecord));off+=hashes.size()*sizeof(PathHashRecord);if(!deepraw.empty()){off=(off+63)&~63ULL;h.fragment_offset=off;h.fragment_count=old_frag_count;h.fragment_record_size=old_frag_rec;h.fragment_crc32c=0;h.flags|=old_deep_flags;off+=deepraw.size();}h.created_unix=(uint64_t)std::time(0);h.header_crc32c=header_crc_v2(h);
 const std::string tmp=idx_path+".tmp";zfec::remove_utf8(tmp);FILE*f=zfec::fopen_utf8(tmp,"wb");if(!f){err="cannot create idx temp file: "+tmp;return false;}bool ok=std::fwrite(&h,1,sizeof(h),f)==sizeof(h);if(ok&&!list_text.empty())ok=std::fwrite(list_text.data(),1,list_text.size(),f)==list_text.size();if(ok&&!info_text.empty())ok=std::fwrite(info_text.data(),1,info_text.size(),f)==info_text.size();if(ok&&!files.empty())ok=std::fwrite(files.data(),sizeof(FileRecord),files.size(),f)==files.size();if(ok&&!pool.empty())ok=std::fwrite(pool.data(),1,pool.size(),f)==pool.size();if(ok&&!hashes.empty())ok=std::fwrite(hashes.data(),sizeof(PathHashRecord),hashes.size(),f)==hashes.size();if(ok&&!deepraw.empty()){uint64_t cur=(uint64_t)std::ftell(f);while(cur<h.fragment_offset){if(std::fputc(0,f)==EOF){ok=false;break;}++cur;}if(ok)ok=std::fwrite(deepraw.data(),1,deepraw.size(),f)==deepraw.size();}if(ok)ok=std::fflush(f)==0;if(std::fclose(f)!=0)ok=false;if(!ok){zfec::remove_utf8(tmp);err="cannot write idx temp file: "+tmp;return false;}return atomic_replace(tmp,idx_path,err);
}
inline bool write_cache_v1_legacy(const std::string&idx_path,const std::string&source,const std::string&list_text,const std::string&info_text,std::string&err){
 Fingerprint fp;if(!fingerprint(source,fp,err))return false;HeaderV1 h;std::memset(&h,0,sizeof(h));std::memcpy(h.magic,"OECIDX1",7);h.version=1;h.header_size=sizeof(h);h.flags=(list_text.empty()?0:kFlagList)|(info_text.empty()?0:kFlagInfo);h.source_size=fp.size;h.source_mtime=fp.mtime;h.source_crc_first=fp.first;h.source_crc_middle=fp.middle;h.source_crc_last=fp.last;h.list_offset=sizeof(h);h.list_size=list_text.size();h.list_crc32c=zfec::crc32c(list_text.data(),list_text.size());h.info_offset=h.list_offset+h.list_size;h.info_size=info_text.size();h.info_crc32c=zfec::crc32c(info_text.data(),info_text.size());h.created_unix=(uint64_t)std::time(0);h.header_crc32c=header_crc_v1(h);const std::string tmp=idx_path+".tmp";zfec::remove_utf8(tmp);FILE*f=zfec::fopen_utf8(tmp,"wb");if(!f){err="cannot create legacy idx temp";return false;}bool ok=std::fwrite(&h,1,sizeof(h),f)==sizeof(h);if(ok&&!list_text.empty())ok=std::fwrite(list_text.data(),1,list_text.size(),f)==list_text.size();if(ok&&!info_text.empty())ok=std::fwrite(info_text.data(),1,info_text.size(),f)==info_text.size();if(std::fclose(f)!=0)ok=false;if(!ok){zfec::remove_utf8(tmp);err="cannot write legacy idx temp";return false;}return atomic_replace(tmp,idx_path,err);
}
// Legacy writer retained for tests/tools; new OEC builds should call write_cache_v2.
inline bool write_cache(const std::string&idx_path,const std::string&source,const std::string&list_text,const std::string&info_text,std::string&err){std::vector<FileInput> none;return write_cache_v2(idx_path,source,list_text,info_text,none,err);}

class MappedFile{public:MappedFile():data_(0),size_(0)
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
,file_(INVALID_HANDLE_VALUE),mapping_(0)
#else
,fd_(-1)
#endif
{}~MappedFile(){close();}MappedFile(const MappedFile&)=delete;MappedFile&operator=(const MappedFile&)=delete;
bool open(const std::string&path,std::string&err){close();
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
 int wn=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,0,0);if(wn<=0){err="idx UTF-8 path conversion failed";return false;}std::vector<wchar_t>w((size_t)wn);MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,w.data(),wn);file_=CreateFileW(w.data(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);if(file_==INVALID_HANDLE_VALUE){err="cannot open idx: "+path;return false;}LARGE_INTEGER li;if(!GetFileSizeEx(file_,&li)||li.QuadPart<=0){err="cannot size idx: "+path;close();return false;}size_=(size_t)li.QuadPart;mapping_=CreateFileMappingW(file_,0,PAGE_READONLY,0,0,0);if(!mapping_){err="CreateFileMapping failed for idx";close();return false;}data_=(const uint8_t*)MapViewOfFile(mapping_,FILE_MAP_READ,0,0,0);if(!data_){err="MapViewOfFile failed for idx";close();return false;}
#else
 fd_=::open(path.c_str(),O_RDONLY);if(fd_<0){err="cannot open idx: "+path;return false;}struct stat st;if(fstat(fd_,&st)!=0||st.st_size<=0){err="cannot size idx: "+path;close();return false;}size_=(size_t)st.st_size;void*p=mmap(0,size_,PROT_READ,MAP_SHARED,fd_,0);if(p==MAP_FAILED){err="mmap failed for idx";close();return false;}data_=(const uint8_t*)p;
#endif
 return true;}void close(){
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
 if(data_)UnmapViewOfFile(data_);if(mapping_)CloseHandle(mapping_);if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);data_=0;mapping_=0;file_=INVALID_HANDLE_VALUE;size_=0;
#else
 if(data_)munmap((void*)data_,size_);if(fd_>=0)::close(fd_);data_=0;size_=0;fd_=-1;
#endif
}const uint8_t*data()const{return data_;}size_t size()const{return size_;}private:const uint8_t*data_;size_t size_;
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
 HANDLE file_,mapping_;
#else
 int fd_;
#endif
};

class Cache{public:Cache():version_(0),h1_(0),h2_(0),valid_(false){}
bool open(const std::string&idx_path,const std::string&source,std::string&err){valid_=false;version_=0;h1_=0;h2_=0;if(!map_.open(idx_path,err))return false;if(map_.size()<16){err="idx truncated";return false;}const char*m=(const char*)map_.data();
 if(std::memcmp(m,"OECIDX2",7)==0){if(map_.size()<sizeof(HeaderV2)){err="idx2 truncated";return false;}h2_=(const HeaderV2*)map_.data();if(h2_->version!=kVersion2||h2_->header_size!=sizeof(HeaderV2)){err="idx2 header/version mismatch";return false;}if(h2_->header_crc32c!=header_crc_v2(*h2_)){err="idx2 header CRC32C mismatch";return false;}if(!range_ok(h2_->list_offset,h2_->list_size)||!range_ok(h2_->info_offset,h2_->info_size)||!range_ok(h2_->file_offset,h2_->file_count*h2_->file_record_size)||!range_ok(h2_->string_offset,h2_->string_size)||!range_ok(h2_->path_hash_offset,h2_->path_hash_count*h2_->path_hash_record_size)){err="idx2 section range invalid";return false;}if(h2_->file_record_size&&h2_->file_record_size!=sizeof(FileRecord)){err="idx2 file record size mismatch";return false;}if(h2_->path_hash_record_size&&h2_->path_hash_record_size!=sizeof(PathHashRecord)){err="idx2 path hash record size mismatch";return false;}if(h2_->list_crc32c!=zfec::crc32c(map_.data()+h2_->list_offset,(size_t)h2_->list_size)||h2_->info_crc32c!=zfec::crc32c(map_.data()+h2_->info_offset,(size_t)h2_->info_size)||h2_->file_crc32c!=zfec::crc32c(map_.data()+h2_->file_offset,(size_t)(h2_->file_count*sizeof(FileRecord)))||h2_->string_crc32c!=zfec::crc32c(map_.data()+h2_->string_offset,(size_t)h2_->string_size)||h2_->path_hash_crc32c!=zfec::crc32c(map_.data()+h2_->path_hash_offset,(size_t)(h2_->path_hash_count*sizeof(PathHashRecord)))){err="idx2 section CRC32C mismatch";return false;}const FileRecord*fr=(const FileRecord*)(map_.data()+h2_->file_offset);for(uint64_t i=0;i<h2_->file_count;++i){if(fr[i].path_offset>h2_->string_size||fr[i].path_size>h2_->string_size-fr[i].path_offset||fr[i].modified_offset>h2_->string_size||fr[i].modified_size>h2_->string_size-fr[i].modified_offset||fr[i].attributes_offset>h2_->string_size||fr[i].attributes_size>h2_->string_size-fr[i].attributes_offset){err="idx2 file/string reference invalid";return false;}}const PathHashRecord*ph=(const PathHashRecord*)(map_.data()+h2_->path_hash_offset);uint64_t prev=0;for(uint64_t i=0;i<h2_->path_hash_count;++i){if(ph[i].file_index>=h2_->file_count){err="idx2 path hash file index invalid";return false;}if(i&&ph[i].hash<prev){err="idx2 path hash table not sorted";return false;}prev=ph[i].hash;}Fingerprint fp;if(!fingerprint(source,fp,err)||!same_fingerprint(*h2_,fp)){if(err.empty())err="idx stale: zero-part fingerprint changed";return false;}version_=2;valid_=true;return true;}
 if(std::memcmp(m,"OECIDX1",7)==0){if(map_.size()<sizeof(HeaderV1)){err="idx1 truncated";return false;}h1_=(const HeaderV1*)map_.data();if(h1_->version!=1||h1_->header_size!=sizeof(HeaderV1)){err="idx1 header/version mismatch";return false;}if(h1_->header_crc32c!=header_crc_v1(*h1_)){err="idx1 header CRC32C mismatch";return false;}if(!range_ok(h1_->list_offset,h1_->list_size)||!range_ok(h1_->info_offset,h1_->info_size)){err="idx1 section range invalid";return false;}if(h1_->list_crc32c!=zfec::crc32c(map_.data()+h1_->list_offset,(size_t)h1_->list_size)||h1_->info_crc32c!=zfec::crc32c(map_.data()+h1_->info_offset,(size_t)h1_->info_size)){err="idx1 section CRC32C mismatch";return false;}Fingerprint fp;if(!fingerprint(source,fp,err)||!same_fingerprint(*h1_,fp)){if(err.empty())err="idx stale: zero-part fingerprint changed";return false;}version_=1;valid_=true;return true;}
 err="idx header/version mismatch";return false;}
bool valid()const{return valid_;}uint32_t version()const{return version_;}bool current()const{return version_==kCurrentVersion;}const HeaderV1*header()const{return h1_;}
bool has_list()const{return valid_&&((version_==1&&(h1_->flags&kFlagList)&&h1_->list_size)||(version_==2&&(h2_->flags&kFlagList)&&h2_->list_size));}bool has_info()const{return valid_&&((version_==1&&(h1_->flags&kFlagInfo)&&h1_->info_size)||(version_==2&&(h2_->flags&kFlagInfo)&&h2_->info_size));}const char*list_data()const{uint64_t o=version_==1?h1_->list_offset:h2_->list_offset;return has_list()?(const char*)(map_.data()+o):0;}size_t list_size()const{return has_list()?(size_t)(version_==1?h1_->list_size:h2_->list_size):0;}const char*info_data()const{uint64_t o=version_==1?h1_->info_offset:h2_->info_offset;return has_info()?(const char*)(map_.data()+o):0;}size_t info_size()const{return has_info()?(size_t)(version_==1?h1_->info_size:h2_->info_size):0;}
bool has_files()const{return valid_&&version_==2&&(h2_->flags&kFlagFiles)&&h2_->file_count;}uint64_t file_count()const{return has_files()?h2_->file_count:0;}const FileRecord*file_records()const{return has_files()?(const FileRecord*)(map_.data()+h2_->file_offset):0;}std::string file_string(uint64_t off,uint32_t n)const{if(version_!=2||off>h2_->string_size||n>h2_->string_size-off)return std::string();return std::string((const char*)map_.data()+h2_->string_offset+(size_t)off,n);}bool lookup_path(const std::string&path,uint64_t&file_index)const{if(version_!=2||!(h2_->flags&kFlagPathHash)||!h2_->path_hash_count)return false;uint64_t key=hash_path(path);const PathHashRecord*p=(const PathHashRecord*)(map_.data()+h2_->path_hash_offset);uint64_t lo=0,hi=h2_->path_hash_count;while(lo<hi){uint64_t mid=lo+(hi-lo)/2;if(p[mid].hash<key)lo=mid+1;else hi=mid;}for(uint64_t i=lo;i<h2_->path_hash_count&&p[i].hash==key;++i){if(p[i].file_index>=h2_->file_count)continue;const FileRecord&r=file_records()[p[i].file_index];if(file_string(r.path_offset,r.path_size)==path){file_index=p[i].file_index;return true;}}return false;}
std::string describe()const{std::ostringstream s;if(version_==1)s<<"OECIDX v1 source_size="<<(unsigned long long)h1_->source_size<<" list_bytes="<<(unsigned long long)h1_->list_size<<" info_bytes="<<(unsigned long long)h1_->info_size<<" created="<<(unsigned long long)h1_->created_unix;else if(version_==2)s<<"OECIDX v2 source_size="<<(unsigned long long)h2_->source_size<<" files="<<(unsigned long long)h2_->file_count<<" path_hash="<<(unsigned long long)h2_->path_hash_count<<" fragments="<<(unsigned long long)h2_->fragment_count<<" blocks="<<(unsigned long long)h2_->block_count<<" list_bytes="<<(unsigned long long)h2_->list_size<<" info_bytes="<<(unsigned long long)h2_->info_size<<" created="<<(unsigned long long)h2_->created_unix;return s.str();}
private:bool range_ok(uint64_t o,uint64_t n)const{return o<=map_.size()&&n<=map_.size()-(size_t)o;}MappedFile map_;uint32_t version_;const HeaderV1*h1_;const HeaderV2*h2_;bool valid_;};
inline bool remove_cache(const std::string&path,std::string&err){if(!zfec::file_exists(path))return true;if(zfec::remove_utf8(path)!=0){err="cannot remove idx: "+path;return false;}return true;}
inline std::string describe(const HeaderV1&h){std::ostringstream s;s<<"OECIDX v1 source_size="<<(unsigned long long)h.source_size<<" list_bytes="<<(unsigned long long)h.list_size<<" info_bytes="<<(unsigned long long)h.info_size<<" created="<<(unsigned long long)h.created_unix;return s.str();}
} // namespace oecidx
