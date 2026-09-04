#pragma once

// zfec.hpp - self-contained Reed-Solomon-like P/Q sidecar protection for files.
// Designed as a small extension layer for zpaqfranz. C++11, no external deps.
// Protects accidental bitrot, not adversarial tampering.

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <limits>
#include <cwchar>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  #include <windows.h>
  #include <io.h>
  #include <sys/stat.h>
  #include <direct.h>
#else
  #include <unistd.h>
  #include <sys/stat.h>
#endif

namespace zfec {

static const uint32_t kVersion = 1;
static const uint32_t kWindowMagic = 0x314E4957u; // "WIN1" little endian
static const uint32_t kFlagCRC32C = 1u;

struct Options {
  uint32_t shard_size;
  uint32_t data_shards;
  uint32_t parity_shards;
  uint32_t stripes_per_window;
  Options(): shard_size(64u * 1024u), data_shards(32), parity_shards(2), stripes_per_window(64) {}
};

#pragma pack(push,1)
struct Header {
  char magic[8];            // "ZFEC001\0"
  uint32_t version;
  uint32_t header_size;
  uint32_t flags;
  uint32_t shard_size;
  uint32_t data_shards;
  uint32_t parity_shards;
  uint32_t stripes_per_window;
  uint32_t reserved0;
  uint64_t file_size;
  uint64_t window_capacity;
  uint64_t window_count;
  uint32_t header_crc32c;
  uint32_t reserved1;
};

struct WindowHeader {
  uint32_t magic;
  uint32_t header_size;
  uint64_t window_index;
  uint64_t data_bytes;
  uint32_t shard_count;
  uint32_t stripe_count;
  uint32_t metadata_bytes;
  uint32_t parity_bytes;
  uint32_t metadata_crc32c;
  uint32_t reserved;
};

#pragma pack(pop)
static_assert(sizeof(Header)==72, "ZFEC Header ABI changed");
static_assert(sizeof(WindowHeader)==48, "ZFEC WindowHeader ABI changed");

struct VerifyStats {
  uint64_t windows = 0;
  uint64_t stripes = 0;
  uint64_t data_shards = 0;
  uint64_t bad_data_shards = 0;
  uint64_t bad_parity_shards = 0;
  uint64_t repairable_stripes = 0;
  uint64_t unrecoverable_stripes = 0;
  bool size_mismatch = false;
  bool ec_corrupt = false;
};

inline uint64_t div_ceil_u64(uint64_t a, uint64_t b) { return a / b + ((a % b) ? 1 : 0); }
inline bool host_little_endian() { const uint16_t x=1; return *reinterpret_cast<const uint8_t*>(&x)==1; }

// OEC stores paths as UTF-8, matching zpaq/zpaqfranz.  On Windows never pass
// those bytes to the ANSI CRT filesystem API: it interprets them in the active
// code page and corrupts names such as Vietnamese/Chinese/Japanese paths.
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
inline std::wstring utf8_to_wide(const std::string& s, bool path_slashes=false) {
  if (s.empty()) return std::wstring();
  int n=MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), 0, 0);
  if(n<=0) return std::wstring();
  std::wstring w((size_t)n, L'\0');
  if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), &w[0], n)!=n) return std::wstring();
  if(path_slashes) for(size_t i=0;i<w.size();++i) if(w[i]==L'/') w[i]=L'\\';
  return w;
}
inline std::string wide_to_utf8(const wchar_t* w) {
  if(!w || !*w) return std::string();
  int n=WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, 0, 0, 0, 0);
  if(n<=0) return std::string();
  std::vector<char> b((size_t)n);
  if(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, b.data(), n, 0, 0)!=n) return std::string();
  return std::string(b.data());
}
inline FILE* fopen_utf8(const std::string& p,const char* mode) {
  const std::wstring wp=utf8_to_wide(p,true), wm=utf8_to_wide(mode?std::string(mode):std::string());
  if((!p.empty()&&wp.empty()) || wm.empty()) return 0;
  return _wfopen(wp.c_str(),wm.c_str());
}
inline int stat64_utf8(const std::string& p, struct _stat64* st) {
  const std::wstring w=utf8_to_wide(p,true); if(!p.empty()&&w.empty()) return -1; return _wstat64(w.c_str(),st);
}
inline int remove_utf8(const std::string& p) { const std::wstring w=utf8_to_wide(p,true); if(!p.empty()&&w.empty()) return -1; return _wremove(w.c_str()); }
inline int rename_utf8(const std::string& a,const std::string& b) { const std::wstring wa=utf8_to_wide(a,true),wb=utf8_to_wide(b,true); if((!a.empty()&&wa.empty())||(!b.empty()&&wb.empty()))return -1; return _wrename(wa.c_str(),wb.c_str()); }
inline int mkdir_utf8(const std::string& p) { const std::wstring w=utf8_to_wide(p,true); if(!p.empty()&&w.empty())return -1; return _wmkdir(w.c_str()); }
inline int rmdir_utf8(const std::string& p) { const std::wstring w=utf8_to_wide(p,true); if(!p.empty()&&w.empty())return -1; return _wrmdir(w.c_str()); }
inline bool replace_file_utf8(const std::string& src,const std::string& dst) {
  const std::wstring ws=utf8_to_wide(src,true),wd=utf8_to_wide(dst,true);
  if((!src.empty()&&ws.empty())||(!dst.empty()&&wd.empty()))return false;
  return MoveFileExW(ws.c_str(),wd.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
#else
inline FILE* fopen_utf8(const std::string& p,const char* mode){return std::fopen(p.c_str(),mode);}
inline int remove_utf8(const std::string& p){return std::remove(p.c_str());}
inline int rename_utf8(const std::string& a,const std::string& b){return std::rename(a.c_str(),b.c_str());}
inline int mkdir_utf8(const std::string& p){return ::mkdir(p.c_str(),0700);}
inline int rmdir_utf8(const std::string& p){return ::rmdir(p.c_str());}
#endif

inline bool file_exists(const std::string& p) {
  FILE* f = fopen_utf8(p, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

inline bool get_file_size(const std::string& p, uint64_t& out) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  struct _stat64 st;
  if (stat64_utf8(p, &st) != 0) return false;
  out = static_cast<uint64_t>(st.st_size);
#else
  struct stat st;
  if (stat(p.c_str(), &st) != 0) return false;
  out = static_cast<uint64_t>(st.st_size);
#endif
  return true;
}

inline bool seek64(FILE* f, uint64_t pos) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  return _fseeki64(f, static_cast<__int64>(pos), SEEK_SET) == 0;
#else
  return fseeko(f, static_cast<off_t>(pos), SEEK_SET) == 0;
#endif
}

inline bool truncate64(FILE* f, uint64_t size) {
  if (!f) return false;
  std::fflush(f);
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  return _chsize_s(_fileno(f), static_cast<__int64>(size)) == 0;
#else
  return ftruncate(fileno(f), static_cast<off_t>(size)) == 0;
#endif
}

inline uint32_t crc32c_update(uint32_t crc, const uint8_t* data, size_t n) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c >> 1) ^ ((c & 1) ? 0x82F63B78u : 0u);
      table[i] = c;
    }
    init = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
  return ~crc;
}
inline uint32_t crc32c(const void* p, size_t n) { return crc32c_update(0, static_cast<const uint8_t*>(p), n); }

struct GF256 {
  uint8_t exp[512];
  uint8_t log[256];
  GF256() {
    std::memset(exp, 0, sizeof(exp));
    std::memset(log, 0, sizeof(log));
    uint16_t x = 1;
    for (int i = 0; i < 255; ++i) {
      exp[i] = static_cast<uint8_t>(x);
      log[static_cast<uint8_t>(x)] = static_cast<uint8_t>(i);
      x <<= 1;
      if (x & 0x100) x ^= 0x11d;
    }
    for (int i = 255; i < 512; ++i) exp[i] = exp[i - 255];
  }
  uint8_t mul(uint8_t a, uint8_t b) const {
    if (!a || !b) return 0;
    return exp[static_cast<unsigned>(log[a]) + static_cast<unsigned>(log[b])];
  }
  uint8_t div(uint8_t a, uint8_t b) const {
    if (!a) return 0;
    if (!b) return 0; // caller guards
    int d = static_cast<int>(log[a]) - static_cast<int>(log[b]);
    if (d < 0) d += 255;
    return exp[d];
  }
  uint8_t coeff(uint32_t lane) const { return exp[lane % 255u]; }
};

inline const GF256& gf() { static GF256 g; return g; }

inline void pq_accumulate(uint8_t* p, uint8_t* q, const uint8_t* data, size_t n, uint8_t coeff) {
  const GF256& g = gf();
  for (size_t i = 0; i < n; ++i) {
    p[i] ^= data[i];
    q[i] ^= g.mul(data[i], coeff);
  }
}

inline bool write_exact(FILE* f, const void* p, size_t n) { return n == 0 || std::fwrite(p, 1, n, f) == n; }
inline bool read_exact(FILE* f, void* p, size_t n) { return n == 0 || std::fread(p, 1, n, f) == n; }

inline uint32_t header_crc(Header h) {
  h.header_crc32c = 0;
  return crc32c(&h, sizeof(h));
}

inline bool validate_options(const Options& o, std::string& err) {
  if (!host_little_endian()) { err = "ZFEC v1 currently requires little-endian host"; return false; }
  if (o.shard_size < 4096 || o.shard_size > 4u * 1024u * 1024u) { err = "shard_size must be 4096..4194304"; return false; }
  if ((o.shard_size & (o.shard_size - 1)) != 0) { err = "shard_size must be a power of two"; return false; }
  if (o.data_shards < 4 || o.data_shards > 128) { err = "data_shards must be 4..128"; return false; }
  if (o.parity_shards != 2) { err = "v1 supports exactly 2 parity shards"; return false; }
  if (o.stripes_per_window < 1 || o.stripes_per_window > 4096) { err = "stripes_per_window must be 1..4096"; return false; }
  return true;
}

inline std::string default_ec_path(const std::string& data) { return data + ".ec"; }

inline bool create(const std::string& data_path, const std::string& ec_path, const Options& opt, bool force, std::string& err) {
  std::string verr;
  if (!validate_options(opt, verr)) { err = verr; return false; }
  if (!file_exists(data_path)) { err = "input does not exist: " + data_path; return false; }
  if (file_exists(ec_path) && !force) { err = "sidecar exists (use --force): " + ec_path; return false; }

  uint64_t fsize = 0;
  if (!get_file_size(data_path, fsize)) { err = "cannot stat input"; return false; }
  const uint64_t window_capacity = static_cast<uint64_t>(opt.shard_size) * opt.data_shards * opt.stripes_per_window;
  const uint64_t window_count = fsize ? div_ceil_u64(fsize, window_capacity) : 1;

  FILE* in = fopen_utf8(data_path, "rb");
  if (!in) { err = "cannot open input"; return false; }
  std::string tmp = ec_path + ".tmp";
  FILE* out = fopen_utf8(tmp, "wb");
  if (!out) { std::fclose(in); err = "cannot create sidecar temp"; return false; }

  Header h{};
  std::memcpy(h.magic, "ZFEC001", 7); h.magic[7] = 0;
  h.version = kVersion;
  h.header_size = sizeof(Header);
  h.flags = kFlagCRC32C;
  h.shard_size = opt.shard_size;
  h.data_shards = opt.data_shards;
  h.parity_shards = opt.parity_shards;
  h.stripes_per_window = opt.stripes_per_window;
  h.file_size = fsize;
  h.window_capacity = window_capacity;
  h.window_count = window_count;
  h.header_crc32c = header_crc(h);
  if (!write_exact(out, &h, sizeof(h))) { err = "write header failed"; std::fclose(in); std::fclose(out); remove_utf8(tmp); return false; }

  std::vector<uint8_t> shard(opt.shard_size);
  uint64_t processed = 0;
  for (uint64_t w = 0; w < window_count; ++w) {
    const uint64_t bytes = std::min<uint64_t>(window_capacity, fsize - processed);
    const uint32_t shard_count = bytes ? static_cast<uint32_t>(div_ceil_u64(bytes, opt.shard_size)) : 0;
    uint32_t stripe_count = shard_count ? static_cast<uint32_t>(div_ceil_u64(shard_count, opt.data_shards)) : 1;
    if (stripe_count > opt.stripes_per_window) stripe_count = opt.stripes_per_window;

    const size_t parity_block = static_cast<size_t>(stripe_count) * opt.shard_size;
    std::vector<uint8_t> p(parity_block, 0), q(parity_block, 0);
    std::vector<uint32_t> data_crc(static_cast<size_t>(stripe_count) * opt.data_shards, 0);
    std::vector<uint32_t> parity_crc(static_cast<size_t>(stripe_count) * 2, 0);

    uint64_t window_done = 0;
    for (uint32_t sidx = 0; sidx < shard_count; ++sidx) {
      std::fill(shard.begin(), shard.end(), 0);
      const size_t want = static_cast<size_t>(std::min<uint64_t>(opt.shard_size, bytes - window_done));
      const size_t got = std::fread(shard.data(), 1, want, in);
      if (got != want) { err = "short read while creating sidecar"; std::fclose(in); std::fclose(out); remove_utf8(tmp); return false; }
      const uint32_t stripe = sidx % stripe_count;
      const uint32_t lane = sidx / stripe_count;
      data_crc[static_cast<size_t>(stripe) * opt.data_shards + lane] = crc32c(shard.data(), shard.size());
      pq_accumulate(&p[static_cast<size_t>(stripe) * opt.shard_size], &q[static_cast<size_t>(stripe) * opt.shard_size], shard.data(), shard.size(), gf().coeff(lane));
      window_done += want;
    }
    for (uint32_t s = 0; s < stripe_count; ++s) {
      parity_crc[static_cast<size_t>(s) * 2] = crc32c(&p[static_cast<size_t>(s) * opt.shard_size], opt.shard_size);
      parity_crc[static_cast<size_t>(s) * 2 + 1] = crc32c(&q[static_cast<size_t>(s) * opt.shard_size], opt.shard_size);
    }

    WindowHeader wh{};
    wh.magic = kWindowMagic;
    wh.header_size = sizeof(WindowHeader);
    wh.window_index = w;
    wh.data_bytes = bytes;
    wh.shard_count = shard_count;
    wh.stripe_count = stripe_count;
    wh.metadata_bytes = static_cast<uint32_t>(data_crc.size() * sizeof(uint32_t) + parity_crc.size() * sizeof(uint32_t));
    wh.parity_bytes = static_cast<uint32_t>((p.size() + q.size()));
    std::vector<uint8_t> meta(wh.metadata_bytes);
    size_t mo = 0;
    if (!data_crc.empty()) { std::memcpy(meta.data() + mo, data_crc.data(), data_crc.size() * sizeof(uint32_t)); mo += data_crc.size() * sizeof(uint32_t); }
    if (!parity_crc.empty()) std::memcpy(meta.data() + mo, parity_crc.data(), parity_crc.size() * sizeof(uint32_t));
    wh.metadata_crc32c = crc32c(meta.data(), meta.size());

    if (!write_exact(out, &wh, sizeof(wh)) || !write_exact(out, meta.data(), meta.size()) ||
        !write_exact(out, p.data(), p.size()) || !write_exact(out, q.data(), q.size())) {
      err = "write sidecar window failed"; std::fclose(in); std::fclose(out); remove_utf8(tmp); return false;
    }
    processed += bytes;
  }

  std::fflush(out);
  std::fclose(in);
  if (std::fclose(out) != 0) { err = "closing sidecar failed"; remove_utf8(tmp); return false; }
  if (force) remove_utf8(ec_path);
  if (rename_utf8(tmp, ec_path) != 0) { err = "rename sidecar temp failed"; remove_utf8(tmp); return false; }
  return true;
}

struct ParsedWindow {
  WindowHeader wh{};
  std::vector<uint32_t> data_crc;
  std::vector<uint32_t> parity_crc;
  std::vector<uint8_t> p, q;
};

inline bool read_header(FILE* ec, Header& h, std::string& err) {
  if (!host_little_endian()) { err = "ZFEC v1 currently requires little-endian host"; return false; }
  if (!read_exact(ec, &h, sizeof(h))) { err = "cannot read EC header"; return false; }
  if (std::memcmp(h.magic, "ZFEC001", 7) != 0 || h.version != 1 || h.header_size != sizeof(Header)) { err = "unsupported/corrupt EC header"; return false; }
  if (h.header_crc32c != header_crc(h)) { err = "EC header CRC32C mismatch"; return false; }
  if (h.parity_shards != 2 || h.data_shards < 4 || h.data_shards > 128 || h.shard_size == 0) { err = "invalid EC parameters"; return false; }
  return true;
}

inline bool read_window(FILE* ec, const Header& h, ParsedWindow& pw, std::string& err) {
  if (!read_exact(ec, &pw.wh, sizeof(pw.wh))) { err = "cannot read EC window header"; return false; }
  if (pw.wh.magic != kWindowMagic || pw.wh.header_size != sizeof(WindowHeader) || pw.wh.stripe_count == 0 || pw.wh.stripe_count > h.stripes_per_window) { err = "corrupt EC window header"; return false; }
  const size_t data_crc_n = static_cast<size_t>(pw.wh.stripe_count) * h.data_shards;
  const size_t parity_crc_n = static_cast<size_t>(pw.wh.stripe_count) * 2;
  const size_t meta_bytes = (data_crc_n + parity_crc_n) * sizeof(uint32_t);
  if (pw.wh.metadata_bytes != meta_bytes) { err = "EC metadata size mismatch"; return false; }
  std::vector<uint8_t> meta(meta_bytes);
  if (!read_exact(ec, meta.data(), meta.size())) { err = "short EC metadata"; return false; }
  if (crc32c(meta.data(), meta.size()) != pw.wh.metadata_crc32c) { err = "EC metadata CRC32C mismatch"; return false; }
  pw.data_crc.resize(data_crc_n);
  pw.parity_crc.resize(parity_crc_n);
  size_t off = 0;
  std::memcpy(pw.data_crc.data(), meta.data(), pw.data_crc.size()*sizeof(uint32_t)); off += pw.data_crc.size()*sizeof(uint32_t);
  std::memcpy(pw.parity_crc.data(), meta.data()+off, pw.parity_crc.size()*sizeof(uint32_t));
  const size_t block = static_cast<size_t>(pw.wh.stripe_count) * h.shard_size;
  if (pw.wh.parity_bytes != block*2) { err = "EC parity size mismatch"; return false; }
  pw.p.resize(block); pw.q.resize(block);
  if (!read_exact(ec, pw.p.data(), pw.p.size()) || !read_exact(ec, pw.q.data(), pw.q.size())) { err = "short EC parity data"; return false; }
  return true;
}

inline uint32_t expected_shard_count(const Header& h, const WindowHeader& wh) {
  return wh.data_bytes ? static_cast<uint32_t>(div_ceil_u64(wh.data_bytes, h.shard_size)) : 0;
}

inline bool verify(const std::string& data_path, const std::string& ec_path, VerifyStats& stats, std::string& err, bool verbose=false) {
  stats = VerifyStats();
  FILE* ec = fopen_utf8(ec_path, "rb");
  if (!ec) { err = "cannot open sidecar: " + ec_path; return false; }
  Header h{};
  if (!read_header(ec, h, err)) { std::fclose(ec); stats.ec_corrupt = true; return false; }
  uint64_t actual_size = 0;
  if (!get_file_size(data_path, actual_size)) { std::fclose(ec); err = "cannot stat data file"; return false; }
  stats.size_mismatch = actual_size != h.file_size;
  FILE* in = fopen_utf8(data_path, "rb");
  if (!in) { std::fclose(ec); err = "cannot open data file"; return false; }
  std::vector<uint8_t> shard(h.shard_size);
  uint64_t logical_pos = 0;
  for (uint64_t w = 0; w < h.window_count; ++w) {
    ParsedWindow pw;
    if (!read_window(ec, h, pw, err)) { std::fclose(in); std::fclose(ec); stats.ec_corrupt = true; return false; }
    if (pw.wh.window_index != w || pw.wh.shard_count != expected_shard_count(h, pw.wh)) { err = "EC window sequence mismatch"; std::fclose(in); std::fclose(ec); stats.ec_corrupt = true; return false; }
    stats.windows++;
    stats.stripes += pw.wh.stripe_count;
    std::vector<uint32_t> bad_data(pw.wh.stripe_count, 0), bad_parity(pw.wh.stripe_count, 0);
    for (uint32_t s = 0; s < pw.wh.stripe_count; ++s) {
      if (crc32c(&pw.p[static_cast<size_t>(s)*h.shard_size], h.shard_size) != pw.parity_crc[static_cast<size_t>(s)*2]) bad_parity[s]++;
      if (crc32c(&pw.q[static_cast<size_t>(s)*h.shard_size], h.shard_size) != pw.parity_crc[static_cast<size_t>(s)*2+1]) bad_parity[s]++;
      stats.bad_parity_shards += bad_parity[s];
    }
    uint64_t remain = pw.wh.data_bytes;
    for (uint32_t i = 0; i < pw.wh.shard_count; ++i) {
      std::fill(shard.begin(), shard.end(), 0);
      const size_t want = static_cast<size_t>(std::min<uint64_t>(h.shard_size, remain));
      size_t got = 0;
      if (logical_pos < actual_size) {
        const uint64_t avail = actual_size - logical_pos;
        const size_t ask = static_cast<size_t>(std::min<uint64_t>(want, avail));
        got = std::fread(shard.data(), 1, ask, in);
        if (got != ask) { err = "data read error"; std::fclose(in); std::fclose(ec); return false; }
      }
      (void)got;
      const uint32_t stripe = i % pw.wh.stripe_count;
      const uint32_t lane = i / pw.wh.stripe_count;
      const uint32_t c = crc32c(shard.data(), shard.size());
      const uint32_t exp = pw.data_crc[static_cast<size_t>(stripe)*h.data_shards + lane];
      if (c != exp) { bad_data[stripe]++; stats.bad_data_shards++; }
      stats.data_shards++;
      logical_pos += want;
      remain -= want;
    }
    for (uint32_t s = 0; s < pw.wh.stripe_count; ++s) {
      if (bad_data[s] == 0) continue;
      const uint32_t goodp = 2 - bad_parity[s];
      bool repairable = (bad_data[s] == 1 && goodp >= 1) || (bad_data[s] == 2 && bad_parity[s] == 0);
      if (repairable) stats.repairable_stripes++; else stats.unrecoverable_stripes++;
    }
    if (verbose) std::fprintf(stderr, "EC verify window %llu: %u shards, %u stripes\n", (unsigned long long)w, pw.wh.shard_count, pw.wh.stripe_count);
  }
  std::fclose(in); std::fclose(ec);
  return true;
}

inline bool copy_prefix(FILE* in, FILE* out, uint64_t bytes, std::vector<uint8_t>& buf, std::string& err) {
  uint64_t left = bytes;
  while (left) {
    const size_t n = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
    const size_t got = std::fread(buf.data(), 1, n, in);
    if (got != n) { err = "short read copying data"; return false; }
    if (!write_exact(out, buf.data(), n)) { err = "write failed copying data"; return false; }
    left -= n;
  }
  return true;
}

inline bool repair(const std::string& data_path, const std::string& ec_path, const std::string& output_path, std::string& err, bool verbose=false) {
  FILE* ec = fopen_utf8(ec_path, "rb");
  if (!ec) { err = "cannot open sidecar"; return false; }
  Header h{};
  if (!read_header(ec, h, err)) { std::fclose(ec); return false; }
  FILE* in = fopen_utf8(data_path, "rb");
  if (!in) { std::fclose(ec); err = "cannot open data"; return false; }
  FILE* out = fopen_utf8(output_path, "wb+");
  if (!out) { std::fclose(in); std::fclose(ec); err = "cannot create repair output"; return false; }

  uint64_t actual_size = 0; get_file_size(data_path, actual_size);
  uint64_t logical_pos = 0;
  std::vector<uint8_t> shard(h.shard_size);
  bool ok = true;
  for (uint64_t w = 0; w < h.window_count && ok; ++w) {
    ParsedWindow pw;
    if (!read_window(ec, h, pw, err)) { ok = false; break; }
    const uint32_t sc = pw.wh.stripe_count;
    const size_t block = static_cast<size_t>(sc) * h.shard_size;
    std::vector<uint8_t> p_good(block, 0), q_good(block, 0);
    std::vector< std::vector<uint32_t> > bad_lanes(sc);
    std::vector<uint8_t> p_bad(sc, 0), q_bad(sc, 0);
    for (uint32_t s = 0; s < sc; ++s) {
      p_bad[s] = crc32c(&pw.p[static_cast<size_t>(s)*h.shard_size], h.shard_size) != pw.parity_crc[static_cast<size_t>(s)*2];
      q_bad[s] = crc32c(&pw.q[static_cast<size_t>(s)*h.shard_size], h.shard_size) != pw.parity_crc[static_cast<size_t>(s)*2+1];
    }

    uint64_t remain = pw.wh.data_bytes;
    const uint64_t window_start = logical_pos;
    for (uint32_t i = 0; i < pw.wh.shard_count; ++i) {
      std::fill(shard.begin(), shard.end(), 0);
      const size_t want = static_cast<size_t>(std::min<uint64_t>(h.shard_size, remain));
      size_t got = 0;
      if (logical_pos < actual_size) {
        const size_t ask = static_cast<size_t>(std::min<uint64_t>(want, actual_size - logical_pos));
        got = std::fread(shard.data(), 1, ask, in);
        if (got != ask) { err = "read error during repair"; ok = false; break; }
      }
      if (!write_exact(out, shard.data(), want)) { err = "write error during repair"; ok = false; break; }
      const uint32_t stripe = i % sc;
      const uint32_t lane = i / sc;
      const uint32_t exp = pw.data_crc[static_cast<size_t>(stripe)*h.data_shards + lane];
      if (crc32c(shard.data(), shard.size()) != exp) {
        bad_lanes[stripe].push_back(lane);
      } else {
        pq_accumulate(&p_good[static_cast<size_t>(stripe)*h.shard_size], &q_good[static_cast<size_t>(stripe)*h.shard_size], shard.data(), shard.size(), gf().coeff(lane));
      }
      logical_pos += want;
      remain -= want;
    }
    if (!ok) break;

    for (uint32_t s = 0; s < sc && ok; ++s) {
      const size_t nb = bad_lanes[s].size();
      if (nb == 0) continue;
      if (nb > 2 || (nb == 2 && (p_bad[s] || q_bad[s])) || (nb == 1 && p_bad[s] && q_bad[s])) {
        std::ostringstream oss; oss << "unrecoverable stripe window=" << w << " stripe=" << s << " bad_data=" << nb << " bad_parity=" << (int)p_bad[s]+(int)q_bad[s];
        err = oss.str(); ok = false; break;
      }
      std::vector<uint8_t> r1(h.shard_size, 0), r2;
      uint8_t* pg = &p_good[static_cast<size_t>(s)*h.shard_size];
      uint8_t* qg = &q_good[static_cast<size_t>(s)*h.shard_size];
      const uint8_t* ps = &pw.p[static_cast<size_t>(s)*h.shard_size];
      const uint8_t* qs = &pw.q[static_cast<size_t>(s)*h.shard_size];
      if (nb == 1) {
        const uint32_t a = bad_lanes[s][0];
        const uint8_t ca = gf().coeff(a);
        for (uint32_t k = 0; k < h.shard_size; ++k) {
          if (!p_bad[s]) r1[k] = ps[k] ^ pg[k];
          else r1[k] = gf().div(static_cast<uint8_t>(qs[k] ^ qg[k]), ca);
        }
        const uint32_t exp = pw.data_crc[static_cast<size_t>(s)*h.data_shards + a];
        if (crc32c(r1.data(), r1.size()) != exp) { err = "recovered shard CRC mismatch"; ok = false; break; }
        const uint64_t physical_index = static_cast<uint64_t>(a) * sc + s;
        if (physical_index >= pw.wh.shard_count) { err = "bad repair mapping"; ok = false; break; }
        const uint64_t off = window_start + physical_index * h.shard_size;
        const size_t valid = static_cast<size_t>(std::min<uint64_t>(h.shard_size, h.file_size - off));
        if (!seek64(out, off) || !write_exact(out, r1.data(), valid) || !seek64(out, logical_pos)) { err = "repair patch write failed"; ok = false; break; }
      } else {
        const uint32_t a = bad_lanes[s][0], b = bad_lanes[s][1];
        const uint8_t ca = gf().coeff(a), cb = gf().coeff(b), denom = static_cast<uint8_t>(ca ^ cb);
        if (!denom) { err = "invalid GF coefficients"; ok = false; break; }
        r2.assign(h.shard_size, 0);
        for (uint32_t k = 0; k < h.shard_size; ++k) {
          const uint8_t sp = static_cast<uint8_t>(ps[k] ^ pg[k]);
          const uint8_t sq = static_cast<uint8_t>(qs[k] ^ qg[k]);
          // Db = (Sq + ca*Sp) / (cb + ca), Da = Sp + Db in GF(2^8)
          const uint8_t db = gf().div(static_cast<uint8_t>(sq ^ gf().mul(ca, sp)), denom);
          r2[k] = db;
          r1[k] = static_cast<uint8_t>(sp ^ db);
        }
        const uint32_t expa = pw.data_crc[static_cast<size_t>(s)*h.data_shards + a];
        const uint32_t expb = pw.data_crc[static_cast<size_t>(s)*h.data_shards + b];
        if (crc32c(r1.data(), r1.size()) != expa || crc32c(r2.data(), r2.size()) != expb) { err = "double-shard recovery CRC mismatch"; ok = false; break; }
        const uint64_t ia = static_cast<uint64_t>(a) * sc + s, ib = static_cast<uint64_t>(b) * sc + s;
        if (ia >= pw.wh.shard_count || ib >= pw.wh.shard_count) { err = "bad double repair mapping"; ok = false; break; }
        const uint64_t offa = window_start + ia*h.shard_size, offb = window_start + ib*h.shard_size;
        const size_t va = static_cast<size_t>(std::min<uint64_t>(h.shard_size, h.file_size - offa));
        const size_t vb = static_cast<size_t>(std::min<uint64_t>(h.shard_size, h.file_size - offb));
        if (!seek64(out, offa) || !write_exact(out, r1.data(), va) || !seek64(out, offb) || !write_exact(out, r2.data(), vb) || !seek64(out, logical_pos)) { err = "double repair write failed"; ok = false; break; }
      }
      if (verbose) std::fprintf(stderr, "Recovered window %llu stripe %u (%zu shard%s)\n", (unsigned long long)w, s, nb, nb==1?"":"s");
    }
  }
  if (ok && !truncate64(out, h.file_size)) { err = "cannot truncate repair output"; ok = false; }
  std::fclose(in); std::fclose(ec); std::fflush(out); std::fclose(out);
  if (!ok) { remove_utf8(output_path); return false; }

  VerifyStats st; std::string verr;
  if (!verify(output_path, ec_path, st, verr, false) || st.bad_data_shards || st.unrecoverable_stripes || st.size_mismatch) {
    err = std::string("post-repair verification failed: ") + verr;
    remove_utf8(output_path);
    return false;
  }
  return true;
}

inline bool info(const std::string& ec_path, std::string& out, std::string& err) {
  FILE* ec = fopen_utf8(ec_path, "rb");
  if (!ec) { err = "cannot open sidecar"; return false; }
  Header h{};
  bool ok = read_header(ec, h, err); std::fclose(ec);
  if (!ok) return false;
  std::ostringstream s;
  s << "ZFEC v" << h.version << "\n"
    << "file_size=" << h.file_size << "\n"
    << "shard_size=" << h.shard_size << "\n"
    << "data_shards=" << h.data_shards << "\n"
    << "parity_shards=" << h.parity_shards << "\n"
    << "stripes_per_window=" << h.stripes_per_window << "\n"
    << "window_capacity=" << h.window_capacity << "\n"
    << "window_count=" << h.window_count << "\n"
    << "nominal_overhead=" << std::fixed << std::setprecision(2) << (100.0 * h.parity_shards / h.data_shards) << "%\n";
  out = s.str(); return true;
}

inline void print_usage(FILE* f) {
  std::fprintf(f,
    "ZFEC sidecar commands:\n"
    "  ec create FILE [--ec PATH] [--data N] [--shard BYTES] [--stripes N] [--force]\n"
    "  ec verify FILE [--ec PATH] [--verbose]\n"
    "  ec repair FILE [--ec PATH] [--output PATH] [--verbose]\n"
    "  ec info SIDECAR.ec\n");
}

inline bool parse_u32(const char* s, uint32_t& v) {
  if (!s || !*s) return false;
  char* e = 0; unsigned long x = std::strtoul(s, &e, 10);
  if (!e || *e || x > std::numeric_limits<uint32_t>::max()) return false;
  v = static_cast<uint32_t>(x); return true;
}

inline int cli(int argc, const char* const* argv) {
  // argv[0] is expected to be "ec" when integrated, or program name in standalone wrapper.
  int base = 1; // argv[0] is program name standalone, or "ec" when integrated.
  if (argc <= base) { print_usage(stderr); return 2; }
  const std::string cmd = argv[base++];
  if (cmd == "info") {
    if (argc <= base) { print_usage(stderr); return 2; }
    std::string o, e;
    if (!info(argv[base], o, e)) { std::fprintf(stderr, "EC error: %s\n", e.c_str()); return 1; }
    std::fputs(o.c_str(), stdout); return 0;
  }
  if (cmd != "create" && cmd != "verify" && cmd != "repair") { print_usage(stderr); return 2; }
  if (argc <= base) { print_usage(stderr); return 2; }
  std::string file = argv[base++], ec = default_ec_path(file), output = file + ".repaired";
  Options opt; bool force = false, verbose = false;
  while (base < argc) {
    std::string a = argv[base++];
    if (a == "--ec" && base < argc) ec = argv[base++];
    else if (a == "--output" && base < argc) output = argv[base++];
    else if (a == "--force") force = true;
    else if (a == "--verbose") verbose = true;
    else if (a == "--data" && base < argc) { if (!parse_u32(argv[base++], opt.data_shards)) { std::fprintf(stderr, "bad --data\n"); return 2; } }
    else if (a == "--shard" && base < argc) { if (!parse_u32(argv[base++], opt.shard_size)) { std::fprintf(stderr, "bad --shard\n"); return 2; } }
    else if (a == "--stripes" && base < argc) { if (!parse_u32(argv[base++], opt.stripes_per_window)) { std::fprintf(stderr, "bad --stripes\n"); return 2; } }
    else { std::fprintf(stderr, "unknown EC option: %s\n", a.c_str()); return 2; }
  }
  std::string err;
  if (cmd == "create") {
    if (!create(file, ec, opt, force, err)) { std::fprintf(stderr, "EC create failed: %s\n", err.c_str()); return 1; }
    uint64_t es=0, ds=0; get_file_size(ec, es); get_file_size(file, ds);
    std::fprintf(stdout, "EC created: %s (%llu bytes, %.2f%% of data)\n", ec.c_str(), (unsigned long long)es, ds?100.0*es/ds:0.0);
    return 0;
  }
  if (cmd == "verify") {
    VerifyStats st;
    if (!verify(file, ec, st, err, verbose)) { std::fprintf(stderr, "EC verify failed: %s\n", err.c_str()); return 1; }
    std::fprintf(stdout, "EC verify: windows=%llu stripes=%llu shards=%llu bad_data=%llu bad_parity=%llu repairable=%llu unrecoverable=%llu size_mismatch=%s\n",
      (unsigned long long)st.windows, (unsigned long long)st.stripes, (unsigned long long)st.data_shards,
      (unsigned long long)st.bad_data_shards, (unsigned long long)st.bad_parity_shards,
      (unsigned long long)st.repairable_stripes, (unsigned long long)st.unrecoverable_stripes, st.size_mismatch?"yes":"no");
    return (st.unrecoverable_stripes || st.size_mismatch || st.ec_corrupt) ? 3 : (st.bad_data_shards ? 2 : 0);
  }
  if (cmd == "repair") {
    if (!repair(file, ec, output, err, verbose)) { std::fprintf(stderr, "EC repair failed: %s\n", err.c_str()); return 1; }
    std::fprintf(stdout, "EC repaired output: %s\n", output.c_str()); return 0;
  }
  return 2;
}

} // namespace zfec
