#pragma once

// oec_idx.hpp - disposable mmap-backed OEC acceleration cache.
// The .000 ZPAQ index remains authoritative. This file may be deleted at any time.
// C++11, no external dependencies.

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

static const uint32_t kVersion = 1;
static const uint32_t kFlagList = 1u << 0;
static const uint32_t kFlagInfo = 1u << 1;
static const size_t kSampleBytes = 64u * 1024u;

#pragma pack(push,1)
struct Header {
  char magic[8];                 // "OECIDX1\0"
  uint32_t version;
  uint32_t header_size;
  uint32_t flags;
  uint32_t reserved0;
  uint64_t source_size;
  int64_t source_mtime;
  uint32_t source_crc_first;
  uint32_t source_crc_middle;
  uint32_t source_crc_last;
  uint32_t reserved1;
  uint64_t list_offset;
  uint64_t list_size;
  uint32_t list_crc32c;
  uint32_t reserved2;
  uint64_t info_offset;
  uint64_t info_size;
  uint32_t info_crc32c;
  uint32_t reserved3;
  uint64_t created_unix;
  uint32_t header_crc32c;
  uint32_t reserved4;
};
#pragma pack(pop)

struct Fingerprint {
  uint64_t size;
  int64_t mtime;
  uint32_t first;
  uint32_t middle;
  uint32_t last;
  Fingerprint(): size(0), mtime(0), first(0), middle(0), last(0) {}
};

inline uint32_t header_crc(Header h) {
  h.header_crc32c = 0;
  return zfec::crc32c(&h, sizeof(h));
}

inline int64_t file_mtime(const std::string& p) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  struct _stat64 st;
  if (_stat64(p.c_str(), &st) != 0) return -1;
  return static_cast<int64_t>(st.st_mtime);
#else
  struct stat st;
  if (stat(p.c_str(), &st) != 0) return -1;
  return static_cast<int64_t>(st.st_mtime);
#endif
}

inline bool sample_crc(FILE* f, uint64_t off, uint64_t source_size, uint32_t& out) {
  if (source_size == 0) { out = 0; return true; }
  if (off >= source_size) off = source_size - 1;
  const uint64_t maxn = std::min<uint64_t>(kSampleBytes, source_size - off);
  std::vector<uint8_t> b(static_cast<size_t>(maxn));
  if (!zfec::seek64(f, off)) return false;
  size_t got = std::fread(b.data(), 1, b.size(), f);
  if (got != b.size()) return false;
  out = zfec::crc32c(b.data(), b.size());
  return true;
}

inline bool fingerprint(const std::string& source, Fingerprint& fp, std::string& err) {
  if (!zfec::get_file_size(source, fp.size)) { err = "cannot stat OEC zero-part index: " + source; return false; }
  fp.mtime = file_mtime(source);
  FILE* f = std::fopen(source.c_str(), "rb");
  if (!f) { err = "cannot open OEC zero-part index: " + source; return false; }
  const uint64_t first_off = 0;
  const uint64_t mid_off = fp.size > kSampleBytes ? (fp.size / 2u) : 0;
  const uint64_t last_off = fp.size > kSampleBytes ? (fp.size - kSampleBytes) : 0;
  bool ok = sample_crc(f, first_off, fp.size, fp.first)
         && sample_crc(f, mid_off, fp.size, fp.middle)
         && sample_crc(f, last_off, fp.size, fp.last);
  std::fclose(f);
  if (!ok) { err = "cannot fingerprint OEC zero-part index: " + source; return false; }
  return true;
}

inline bool same_fingerprint(const Header& h, const Fingerprint& f) {
  return h.source_size == f.size && h.source_mtime == f.mtime
      && h.source_crc_first == f.first
      && h.source_crc_middle == f.middle
      && h.source_crc_last == f.last;
}

inline bool atomic_replace(const std::string& tmp, const std::string& dst, std::string& err) {
  const std::string bak = dst + ".oecidx.bak";
  const bool had = zfec::file_exists(dst);
  if (had) {
    std::remove(bak.c_str());
    if (std::rename(dst.c_str(), bak.c_str()) != 0) { err = "cannot move old idx aside: " + dst; return false; }
  }
  if (std::rename(tmp.c_str(), dst.c_str()) != 0) {
    if (had) std::rename(bak.c_str(), dst.c_str());
    err = "cannot install idx: " + dst;
    return false;
  }
  if (had) std::remove(bak.c_str());
  return true;
}

inline bool write_cache(const std::string& idx_path, const std::string& source,
                        const std::string& list_text, const std::string& info_text,
                        std::string& err) {
  Fingerprint fp;
  if (!fingerprint(source, fp, err)) return false;
  Header h;
  std::memset(&h, 0, sizeof(h));
  std::memcpy(h.magic, "OECIDX1", 7);
  h.version = kVersion;
  h.header_size = static_cast<uint32_t>(sizeof(h));
  h.flags = (list_text.empty() ? 0u : kFlagList) | (info_text.empty() ? 0u : kFlagInfo);
  h.source_size = fp.size;
  h.source_mtime = fp.mtime;
  h.source_crc_first = fp.first;
  h.source_crc_middle = fp.middle;
  h.source_crc_last = fp.last;
  h.list_offset = sizeof(h);
  h.list_size = static_cast<uint64_t>(list_text.size());
  h.list_crc32c = zfec::crc32c(list_text.data(), list_text.size());
  h.info_offset = h.list_offset + h.list_size;
  h.info_size = static_cast<uint64_t>(info_text.size());
  h.info_crc32c = zfec::crc32c(info_text.data(), info_text.size());
  h.created_unix = static_cast<uint64_t>(std::time(0));
  h.header_crc32c = header_crc(h);

  const std::string tmp = idx_path + ".tmp";
  std::remove(tmp.c_str());
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) { err = "cannot create idx temp file: " + tmp; return false; }
  bool ok = std::fwrite(&h, 1, sizeof(h), f) == sizeof(h);
  if (ok && !list_text.empty()) ok = std::fwrite(list_text.data(), 1, list_text.size(), f) == list_text.size();
  if (ok && !info_text.empty()) ok = std::fwrite(info_text.data(), 1, info_text.size(), f) == info_text.size();
  if (ok) ok = std::fflush(f) == 0;
  if (std::fclose(f) != 0) ok = false;
  if (!ok) { std::remove(tmp.c_str()); err = "cannot write idx temp file: " + tmp; return false; }
  return atomic_replace(tmp, idx_path, err);
}

class MappedFile {
public:
  MappedFile(): data_(0), size_(0)
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    , file_(INVALID_HANDLE_VALUE), mapping_(0)
#else
    , fd_(-1)
#endif
  {}
  ~MappedFile() { close(); }
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  bool open(const std::string& path, std::string& err) {
    close();
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, 0, 0);
    if (wn <= 0) { err = "idx UTF-8 path conversion failed"; return false; }
    std::vector<wchar_t> w(static_cast<size_t>(wn));
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), wn);
    file_ = CreateFileW(w.data(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file_ == INVALID_HANDLE_VALUE) { err = "cannot open idx: " + path; return false; }
    LARGE_INTEGER li;
    if (!GetFileSizeEx(file_, &li) || li.QuadPart <= 0) { err = "cannot size idx: " + path; close(); return false; }
    size_ = static_cast<size_t>(li.QuadPart);
    mapping_ = CreateFileMappingW(file_, 0, PAGE_READONLY, 0, 0, 0);
    if (!mapping_) { err = "CreateFileMapping failed for idx"; close(); return false; }
    data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
    if (!data_) { err = "MapViewOfFile failed for idx"; close(); return false; }
#else
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) { err = "cannot open idx: " + path; return false; }
    struct stat st;
    if (fstat(fd_, &st) != 0 || st.st_size <= 0) { err = "cannot size idx: " + path; close(); return false; }
    size_ = static_cast<size_t>(st.st_size);
    void* p = mmap(0, size_, PROT_READ, MAP_SHARED, fd_, 0);
    if (p == MAP_FAILED) { err = "mmap failed for idx"; close(); return false; }
    data_ = static_cast<const uint8_t*>(p);
#endif
    return true;
  }

  void close() {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    if (data_) UnmapViewOfFile(data_);
    if (mapping_) CloseHandle(mapping_);
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    data_ = 0; mapping_ = 0; file_ = INVALID_HANDLE_VALUE; size_ = 0;
#else
    if (data_) munmap(const_cast<uint8_t*>(data_), size_);
    if (fd_ >= 0) ::close(fd_);
    data_ = 0; size_ = 0; fd_ = -1;
#endif
  }
  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }
private:
  const uint8_t* data_;
  size_t size_;
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  HANDLE file_;
  HANDLE mapping_;
#else
  int fd_;
#endif
};

class Cache {
public:
  Cache(): h_(0), valid_(false) {}
  bool open(const std::string& idx_path, const std::string& source, std::string& err) {
    valid_ = false; h_ = 0; idx_path_ = idx_path; source_ = source;
    if (!map_.open(idx_path, err)) return false;
    if (map_.size() < sizeof(Header)) { err = "idx truncated"; return false; }
    h_ = reinterpret_cast<const Header*>(map_.data());
    if (std::memcmp(h_->magic, "OECIDX1", 7) != 0 || h_->version != kVersion || h_->header_size != sizeof(Header)) {
      err = "idx header/version mismatch"; return false;
    }
    if (h_->header_crc32c != header_crc(*h_)) { err = "idx header CRC32C mismatch"; return false; }
    if (!range_ok(h_->list_offset, h_->list_size) || !range_ok(h_->info_offset, h_->info_size)) {
      err = "idx section range invalid"; return false;
    }
    if (h_->list_crc32c != zfec::crc32c(map_.data()+h_->list_offset, static_cast<size_t>(h_->list_size))) {
      err = "idx list section CRC32C mismatch"; return false;
    }
    if (h_->info_crc32c != zfec::crc32c(map_.data()+h_->info_offset, static_cast<size_t>(h_->info_size))) {
      err = "idx info section CRC32C mismatch"; return false;
    }
    Fingerprint fp;
    if (!fingerprint(source, fp, err)) return false;
    if (!same_fingerprint(*h_, fp)) { err = "idx stale: zero-part fingerprint changed"; return false; }
    valid_ = true;
    return true;
  }
  bool valid() const { return valid_; }
  const Header* header() const { return h_; }
  bool has_list() const { return valid_ && (h_->flags & kFlagList) && h_->list_size; }
  bool has_info() const { return valid_ && (h_->flags & kFlagInfo) && h_->info_size; }
  const char* list_data() const { return has_list() ? reinterpret_cast<const char*>(map_.data()+h_->list_offset) : 0; }
  size_t list_size() const { return has_list() ? static_cast<size_t>(h_->list_size) : 0; }
  const char* info_data() const { return has_info() ? reinterpret_cast<const char*>(map_.data()+h_->info_offset) : 0; }
  size_t info_size() const { return has_info() ? static_cast<size_t>(h_->info_size) : 0; }
private:
  bool range_ok(uint64_t off, uint64_t len) const {
    return off <= map_.size() && len <= map_.size() - static_cast<size_t>(off);
  }
  MappedFile map_;
  const Header* h_;
  bool valid_;
  std::string idx_path_, source_;
};

inline bool remove_cache(const std::string& path, std::string& err) {
  if (!zfec::file_exists(path)) return true;
  if (std::remove(path.c_str()) != 0) { err = "cannot remove idx: " + path; return false; }
  return true;
}

inline std::string describe(const Header& h) {
  std::ostringstream s;
  s << "OECIDX v" << h.version
    << " source_size=" << static_cast<unsigned long long>(h.source_size)
    << " list_bytes=" << static_cast<unsigned long long>(h.list_size)
    << " info_bytes=" << static_cast<unsigned long long>(h.info_size)
    << " created=" << static_cast<unsigned long long>(h.created_unix);
  return s.str();
}

} // namespace oecidx
