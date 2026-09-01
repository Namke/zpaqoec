#pragma once

#include "zfec.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#if defined(_WIN32)
  #include <process.h>
#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
#endif

namespace zfext {

static const int kNotHandled = -777777;

inline std::string zero_suffix(uint64_t n, uint32_t digits) {
  std::ostringstream s; s << std::setw(static_cast<int>(digits)) << std::setfill('0') << n; return s.str();
}
inline std::string qmarks(uint32_t digits) { return std::string(digits, '?'); }
inline bool path_exists(const std::string& p) { return zfec::file_exists(p); }

struct PatternParts {
  std::string prefix;
  std::string suffix;
  uint32_t digits = 0;
};

inline bool parse_qmark_pattern(const std::string& pattern, PatternParts& out, std::string& err) {
  const size_t q = pattern.find('?');
  if (q == std::string::npos) { err = "archive pattern must contain ? digits"; return false; }
  size_t e = q;
  while (e < pattern.size() && pattern[e] == '?') ++e;
  if (pattern.find('?', e) != std::string::npos) { err = "only one contiguous ? run is supported"; return false; }
  const size_t n = e - q;
  if (n < 1 || n > 9) { err = "? digit count must be 1..9"; return false; }
  out.prefix = pattern.substr(0, q);
  out.suffix = pattern.substr(e);
  out.digits = static_cast<uint32_t>(n);
  return true;
}

inline std::string pattern_number(const PatternParts& p, uint64_t n) {
  return p.prefix + zero_suffix(n, p.digits) + p.suffix;
}

inline std::string infer_index_from_pattern(const PatternParts& p) {
  return pattern_number(p, 0);
}

inline std::string state_path(const std::string& base) { return base + ".ecstate"; }

inline bool read_state(const std::string& base, uint64_t& last) {
  FILE* f=std::fopen(state_path(base).c_str(), "rb");
  if (!f) return false;
  char magic[16]={0}; unsigned long long v=0;
  bool ok = std::fgets(magic,sizeof(magic),f) && std::strncmp(magic,"ZFEXT1",6)==0 && std::fscanf(f,"last_part=%llu",&v)==1;
  std::fclose(f);
  if (ok) last=static_cast<uint64_t>(v);
  return ok;
}

inline bool write_state(const std::string& base, uint64_t last) {
  const std::string p=state_path(base), tmp=p+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb"); if(!f) return false;
  std::fprintf(f,"ZFEXT1\nlast_part=%llu\n",(unsigned long long)last);
  std::fflush(f);
  if(std::fclose(f)!=0){ std::remove(tmp.c_str()); return false; }
  std::remove(p.c_str());
  if(std::rename(tmp.c_str(),p.c_str())!=0){ std::remove(tmp.c_str()); return false; }
  return true;
}

inline uint64_t recover_last_part_by_names(const std::string& base, uint32_t digits, uint64_t maxn) {
  // One-time recovery path only. Normal operation uses .ecstate and never walks old parts.
  uint64_t n=1;
  while(n<=maxn && path_exists(base+"."+zero_suffix(n,digits))) ++n;
  return n-1;
}

inline int spawn_self(const std::string& exe, const std::vector<std::string>& args) {
  std::vector<const char*> av;
  av.reserve(args.size()+2);
  av.push_back(exe.c_str());
  for (size_t i=0;i<args.size();++i) av.push_back(args[i].c_str());
  av.push_back(0);
#if defined(_WIN32)
  intptr_t r = _spawnv(_P_WAIT, exe.c_str(), av.data());
  return r < 0 ? 127 : static_cast<int>(r);
#else
  pid_t pid = fork();
  if (pid < 0) return 127;
  if (pid == 0) {
    execvp(exe.c_str(), const_cast<char* const*>(av.data()));
    _exit(127);
  }
  int status=0;
  if (waitpid(pid, &status, 0) < 0) return 127;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 127;
#endif
}

inline void trunkadd_usage() {
  std::fprintf(stderr,
    "Indexed multipart + EC:\n"
    "  trunkadd BASE <zpaq add source/options...> [--ec-data N] [--ec-shard BYTES]\n"
    "           [--ec-stripes N] [--digits N] [--no-trunk-ec] [--no-part-ec]\n\n"
    "Example:\n"
    "  zpaqfranz trunkadd compress /data -method 5\n\n"
    "Creates/updates compress.000 and one new compress.NNN, then writes\n"
    "compress.NNN.ec (and compress.000.ec unless --no-trunk-ec).\n");
}

inline int trunkadd(int argc, const char* const* argv) {
  if (argc < 3) { trunkadd_usage(); return 2; }
  const std::string exe = argv[0];
  const std::string base = argv[2];
  uint32_t digits=3;
  zfec::Options ecopt;
  bool protect_trunk=true, protect_part=true;
  std::vector<std::string> pass;
  for (int i=3;i<argc;++i) {
    std::string a=argv[i];
    if (a=="--digits" && i+1<argc) {
      if (!zfec::parse_u32(argv[++i], digits) || digits<1 || digits>9) { std::fprintf(stderr,"bad --digits\n"); return 2; }
    } else if (a=="--ec-data" && i+1<argc) {
      if (!zfec::parse_u32(argv[++i], ecopt.data_shards)) { std::fprintf(stderr,"bad --ec-data\n"); return 2; }
    } else if (a=="--ec-shard" && i+1<argc) {
      if (!zfec::parse_u32(argv[++i], ecopt.shard_size)) { std::fprintf(stderr,"bad --ec-shard\n"); return 2; }
    } else if (a=="--ec-stripes" && i+1<argc) {
      if (!zfec::parse_u32(argv[++i], ecopt.stripes_per_window)) { std::fprintf(stderr,"bad --ec-stripes\n"); return 2; }
    } else if (a=="--no-trunk-ec") protect_trunk=false;
    else if (a=="--no-part-ec") protect_part=false;
    else if (a=="-index" || a=="--index") {
      std::fprintf(stderr,"trunkadd owns -index; do not pass another index path\n"); return 2;
    } else pass.push_back(a);
  }
  std::string verr;
  if (!zfec::validate_options(ecopt, verr)) { std::fprintf(stderr,"bad EC options: %s\n",verr.c_str()); return 2; }
  if (pass.empty()) { std::fprintf(stderr,"trunkadd: missing source/add arguments\n"); return 2; }

  const std::string pattern = base + "." + qmarks(digits);
  const std::string index = base + "." + zero_suffix(0,digits);

  uint64_t maxn=1;
  for (uint32_t i=0;i<digits;++i) maxn*=10;
  maxn-=1;
  uint64_t last=0;
  if (!read_state(base,last)) {
    if (path_exists(index)) {
      last=recover_last_part_by_names(base,digits,maxn);
      std::fprintf(stdout,"trunkadd: recovered missing .ecstate once (last_part=%llu)\n",(unsigned long long)last);
    }
  }
  const uint64_t next=last+1;
  if (next>maxn) { std::fprintf(stderr,"trunkadd: part number space exhausted\n"); return 1; }
  const std::string expected_part = base + "." + zero_suffix(next,digits);

  std::vector<std::string> child;
  child.push_back("a"); child.push_back(pattern);
  child.insert(child.end(), pass.begin(), pass.end());
  child.push_back("-index"); child.push_back(index);

  std::fprintf(stdout,"trunkadd: index=%s next=%s\n", index.c_str(), expected_part.c_str());
  const int rc=spawn_self(exe, child);
  if (rc!=0) { std::fprintf(stderr,"trunkadd: zpaq add failed rc=%d; EC not written\n",rc); return rc; }
  if (!path_exists(expected_part)) {
    std::fprintf(stderr,"trunkadd: add succeeded but expected part %s was not found; .ecstate may be stale; refusing to guess\n",expected_part.c_str());
    return 4;
  }
  if (!write_state(base,next)) {
    std::fprintf(stderr,"trunkadd: part was added but could not update %s; next run will recover once from filenames\n",state_path(base).c_str());
  }

  std::string err;
  if (protect_part) {
    if (!zfec::create(expected_part, zfec::default_ec_path(expected_part), ecopt, true, err)) {
      std::fprintf(stderr,"trunkadd: archive part is valid but EC creation failed: %s\n",err.c_str()); return 5;
    }
    std::fprintf(stdout,"trunkadd: protected %s -> %s.ec\n",expected_part.c_str(),expected_part.c_str());
  }
  if (protect_trunk && path_exists(index)) {
    if (!zfec::create(index, zfec::default_ec_path(index), ecopt, true, err)) {
      std::fprintf(stderr,"trunkadd: part EC OK, trunk EC failed: %s\n",err.c_str()); return 6;
    }
    std::fprintf(stdout,"trunkadd: protected trunk %s -> %s.ec\n",index.c_str(),index.c_str());
  }
  return 0;
}


inline void trunkinit_usage() {
  std::fprintf(stderr,
    "Retrofit existing multipart archive with trunk/index + EC:\n"
    "  trunkinit ARCHIVE_PATTERN [-index INDEX] [--force]\n"
    "            [--ec-data N] [--ec-shard BYTES] [--ec-stripes N]\n"
    "            [--no-trunk-ec] [--no-part-ec] [zpaq read options...]\n\n"
    "Examples:\n"
    "  zpaqfranz trunkinit \"compress.???\"\n"
    "  zpaqfranz trunkinit \"backup_????????.zpaq\" -index backup_00000000.zpaq\n"
    "  zpaqfranz trunkinit \"secret.???\" -key PASSWORD\n\n"
    "The index path defaults to the archive pattern with ? replaced by 0.\n"
    "Index generation uses the native ZPAQ extract -index path, so archive parts\n"
    "are never rewritten. EC sidecars are generated independently for every part.\n");
}

inline bool replace_file_safely(const std::string& tmp, const std::string& dst, std::string& err) {
  const std::string bak = dst + ".trunkinit.bak";
  const bool had = path_exists(dst);
  if (had) {
    std::remove(bak.c_str());
    if (std::rename(dst.c_str(), bak.c_str()) != 0) { err = "cannot move old index aside: " + dst; return false; }
  }
  if (std::rename(tmp.c_str(), dst.c_str()) != 0) {
    if (had) std::rename(bak.c_str(), dst.c_str());
    err = "cannot install rebuilt index: " + dst;
    return false;
  }
  if (had) std::remove(bak.c_str());
  return true;
}

inline int trunkinit(int argc, const char* const* argv) {
  if (argc < 3) { trunkinit_usage(); return 2; }
  const std::string exe = argv[0];
  std::string pattern = argv[2];
  if (pattern.find('?') == std::string::npos) pattern += ".???";

  PatternParts pp;
  std::string perr;
  if (!parse_qmark_pattern(pattern, pp, perr)) { std::fprintf(stderr, "trunkinit: %s\n", perr.c_str()); return 2; }

  std::string index = infer_index_from_pattern(pp);
  bool force = false, protect_trunk = true, protect_parts = true;
  zfec::Options ecopt;
  std::vector<std::string> readopts;
  for (int i=3;i<argc;++i) {
    const std::string a = argv[i];
    if ((a=="-index" || a=="--index") && i+1<argc) index = argv[++i];
    else if (a=="--force") force = true;
    else if (a=="--ec-data" && i+1<argc) {
      if (!zfec::parse_u32(argv[++i], ecopt.data_shards)) { std::fprintf(stderr,"bad --ec-data\n"); return 2; }
    } else if (a=="--ec-shard" && i+1<argc) {
      if (!zfec::parse_u32(argv[++i], ecopt.shard_size)) { std::fprintf(stderr,"bad --ec-shard\n"); return 2; }
    } else if (a=="--ec-stripes" && i+1<argc) {
      if (!zfec::parse_u32(argv[++i], ecopt.stripes_per_window)) { std::fprintf(stderr,"bad --ec-stripes\n"); return 2; }
    } else if (a=="--no-trunk-ec") protect_trunk = false;
    else if (a=="--no-part-ec") protect_parts = false;
    else readopts.push_back(a);
  }

  std::string verr;
  if (!zfec::validate_options(ecopt, verr)) { std::fprintf(stderr,"bad EC options: %s\n",verr.c_str()); return 2; }
  const std::string first = pattern_number(pp, 1);
  if (!path_exists(first)) { std::fprintf(stderr,"trunkinit: first archive part not found: %s\n",first.c_str()); return 3; }
  if (path_exists(index) && !force) {
    std::fprintf(stderr,"trunkinit: index already exists: %s (use --force to rebuild)\n",index.c_str());
    return 3;
  }

  // Build to a temporary path. Native `x -index` reads archive metadata but does not extract files.
  const std::string tmpindex = index + ".trunkinit.tmp";
  std::remove(tmpindex.c_str());
  std::vector<std::string> child;
  child.push_back("x"); child.push_back(pattern);
  child.insert(child.end(), readopts.begin(), readopts.end());
  child.push_back("-index"); child.push_back(tmpindex);
  child.push_back("-force");
  std::fprintf(stdout,"trunkinit: rebuilding index %s from %s\n",index.c_str(),pattern.c_str());
  const int rc = spawn_self(exe, child);
  if (rc != 0) {
    std::remove(tmpindex.c_str());
    std::fprintf(stderr,"trunkinit: native index rebuild failed rc=%d; archive parts were not modified\n",rc);
    return rc;
  }
  if (!path_exists(tmpindex)) {
    std::fprintf(stderr,"trunkinit: native index rebuild returned success but did not create %s\n",tmpindex.c_str());
    return 4;
  }
  if (!replace_file_safely(tmpindex, index, verr)) { std::fprintf(stderr,"trunkinit: %s\n",verr.c_str()); return 4; }

  uint64_t maxn = 1;
  for (uint32_t i=0;i<pp.digits;++i) maxn *= 10;
  maxn -= 1;
  uint64_t last = 0, created = 0, skipped = 0;
  std::string err;
  if (protect_parts) {
    for (uint64_t n=1;n<=maxn;++n) {
      const std::string part = pattern_number(pp,n);
      if (!path_exists(part)) { last = n-1; break; }
      last = n;
      const std::string ec = zfec::default_ec_path(part);
      if (path_exists(ec) && !force) {
        ++skipped;
        std::fprintf(stdout,"trunkinit: EC exists, skip %s\n",ec.c_str());
        continue;
      }
      if (!zfec::create(part, ec, ecopt, true, err)) {
        std::fprintf(stderr,"trunkinit: index is ready, but EC creation failed for %s: %s\n",part.c_str(),err.c_str());
        return 5;
      }
      ++created;
      std::fprintf(stdout,"trunkinit: protected %s -> %s\n",part.c_str(),ec.c_str());
    }
  } else {
    for (uint64_t n=1;n<=maxn;++n) {
      const std::string part = pattern_number(pp,n);
      if (!path_exists(part)) { last=n-1; break; }
      last=n;
    }
  }
  if (last == 0) { std::fprintf(stderr,"trunkinit: no archive parts found after index rebuild\n"); return 4; }

  if (protect_trunk) {
    const std::string ec = zfec::default_ec_path(index);
    if (path_exists(ec) && !force) {
      ++skipped;
      std::fprintf(stdout,"trunkinit: trunk EC exists, skip %s\n",ec.c_str());
    } else if (!zfec::create(index, ec, ecopt, true, err)) {
      std::fprintf(stderr,"trunkinit: part EC ready, trunk EC failed: %s\n",err.c_str());
      return 6;
    } else {
      ++created;
      std::fprintf(stdout,"trunkinit: protected trunk %s -> %s\n",index.c_str(),ec.c_str());
    }
  }

  // If this is the extension's conventional BASE.??? layout, seed trunkadd state too.
  if (pp.suffix.empty() && pp.prefix.size() >= 1 && pp.prefix[pp.prefix.size()-1] == '.') {
    const std::string base = pp.prefix.substr(0, pp.prefix.size()-1);
    if (!base.empty() && !write_state(base,last))
      std::fprintf(stderr,"trunkinit: warning: could not write %s\n",state_path(base).c_str());
  }
  std::fprintf(stdout,"trunkinit: DONE parts=%llu ec_created=%llu ec_skipped=%llu index=%s\n",
    (unsigned long long)last,(unsigned long long)created,(unsigned long long)skipped,index.c_str());
  return 0;
}

inline int dispatch_const(int argc, const char* const* argv) {
  if (argc < 2 || !argv || !argv[1]) return kNotHandled;
  const std::string cmd=argv[1];
  if (cmd=="ec") return zfec::cli(argc-1, argv+1);
  if (cmd=="trunkadd") return trunkadd(argc, argv);
  if (cmd=="trunkinit") return trunkinit(argc, argv);
  return kNotHandled;
}

template <class ArgvT>
inline int dispatch(int argc, ArgvT argv) {
  std::vector<const char*> av;
  av.reserve(argc > 0 ? static_cast<size_t>(argc) : 0);
  for (int i=0;i<argc;++i) av.push_back(argv[i]);
  return dispatch_const(argc, av.empty()?0:av.data());
}

} // namespace zfext
