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

inline int dispatch_const(int argc, const char* const* argv) {
  if (argc < 2 || !argv || !argv[1]) return kNotHandled;
  const std::string cmd=argv[1];
  if (cmd=="ec") return zfec::cli(argc-1, argv+1);
  if (cmd=="trunkadd") return trunkadd(argc, argv);
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
