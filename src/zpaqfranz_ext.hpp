#pragma once

#include "zfec.hpp"
#include "oec_idx.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  #include <process.h>
#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
#endif

namespace zfext {

static const int kNotHandled = -777777;
static const char* const kOecOverlayVersion = "0.3.3";

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

inline size_t last_path_separator(const std::string& p) {
  const size_t a=p.find_last_of('/');
  const size_t b=p.find_last_of('\\');
  if(a==std::string::npos) return b;
  if(b==std::string::npos) return a;
  return a>b?a:b;
}

// A legacy single-file archive keeps its original payload name. OEC inserts
// the logical zero-part marker before the final extension so foo.zpaq becomes
// foo.000.zpaq. The data bytes are never renamed or rewritten by oecinit.
inline std::string infer_single_index(const std::string& archive) {
  const size_t sep=last_path_separator(archive);
  const size_t dot=archive.find_last_of('.');
  if(dot!=std::string::npos && (sep==std::string::npos || dot>sep+0))
    return archive.substr(0,dot)+".000"+archive.substr(dot);
  return archive+".000";
}

inline std::string infer_single_idx(const std::string& archive) {
  const size_t sep=last_path_separator(archive);
  const size_t dot=archive.find_last_of('.');
  if(dot!=std::string::npos && (sep==std::string::npos || dot>sep+0))
    return archive.substr(0,dot)+".idx";
  return archive+".idx";
}



inline bool oec_ascii_ieq_suffix(const std::string& s, const char* suffix) {
  const size_t n=std::strlen(suffix), m=s.size();
  if(m<n) return false;
  for(size_t i=0;i<n;++i) {
    char a=s[m-n+i], b=suffix[i];
    if(a>='A'&&a<='Z') a=static_cast<char>(a-'A'+'a');
    if(b>='A'&&b<='Z') b=static_cast<char>(b-'A'+'a');
    if(a!=b) return false;
  }
  return true;
}

inline std::string oec_password_stem_from_archive(const std::string& archive_spec) {
  const size_t sep=last_path_separator(archive_spec);
  std::string name=archive_spec.substr(sep==std::string::npos?0:sep+1);
  if(oec_ascii_ieq_suffix(name,".zpaq")) name.resize(name.size()-5);

  // Patterns name the whole archive set. test???.zpaq -> test.password and
  // compress.??? -> compress.password. Trim separators left immediately before
  // the wildcard run so backup_????????.zpaq maps to backup.password.
  const size_t q=name.find('?');
  if(q!=std::string::npos) name.resize(q);

  // A caller can also address an OEC zero part directly. foo.000.zpaq should
  // still use foo.password, not foo.000.password. Only all-zero numeric suffixes
  // are removed here to avoid changing ordinary names such as movie.2026.zpaq.
  size_t cut=name.find_last_of("._-");
  if(cut!=std::string::npos && cut+1<name.size()) {
    const std::string tail=name.substr(cut+1);
    bool allzero=tail.size()>=3 && tail.size()<=9;
    for(size_t i=0;i<tail.size() && allzero;++i) allzero=(tail[i]=='0');
    if(allzero) name.resize(cut);
  }
  while(!name.empty()) {
    const char c=name[name.size()-1];
    if(c=='.'||c=='_'||c=='-'||c==' '||c=='\t') name.resize(name.size()-1);
    else break;
  }
  return name;
}

inline bool oec_has_explicit_auth(int argc, const char* const* argv) {
  if(!argv) return false;
  for(int i=1;i<argc;++i) {
    if(!argv[i]) continue;
    const std::string a=argv[i];
    if((a=="-key" || a=="-franzen") && i+1<argc && argv[i+1] && *argv[i+1]) return true;
  }
  return false;
}

inline std::string oec_archive_hint_for_password(int argc, const char* const* argv) {
  if(!argv || argc<2 || !argv[1]) return std::string();
  const std::string cmd=argv[1];
  if(cmd=="oec_idx") return argc>=4 && argv[3] ? argv[3] : std::string();
  if(cmd=="oecinit" || cmd=="oec_init" || cmd=="oec_a" || cmd=="oec_l" || cmd=="oec_i" || cmd=="oec_x" || cmd=="oec_e")
    return argc>=3 && argv[2] ? argv[2] : std::string();
  // Native archive commands remain untouched by OEC after this preflight, but
  // they benefit from the same password-folder lookup before upstream prompts.
  if(cmd=="a" || cmd=="e" || cmd=="l" || cmd=="x" || cmd=="i" || cmd=="t")
    return argc>=3 && argv[2] ? argv[2] : std::string();
  return std::string();
}

inline bool oec_set_franzkey_env(const std::string& password) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  return _putenv_s("FRANZKEY",password.c_str())==0;
#else
  return setenv("FRANZKEY",password.c_str(),1)==0;
#endif
}

inline bool oec_read_first_password_line(const std::string& path, std::string& password, std::string& err) {
  password.clear(); err.clear();
  FILE* f=std::fopen(path.c_str(),"rb");
  if(!f) { err="cannot open password file"; return false; }
  char buf[65536];
  if(!std::fgets(buf,sizeof(buf),f)) { std::fclose(f); err="password file is empty/unreadable"; return false; }
  const bool truncated=(std::strchr(buf,'\n')==0 && !std::feof(f));
  std::fclose(f);
  if(truncated) { err="password line is too long"; return false; }
  password=buf;
  while(!password.empty() && (password[password.size()-1]=='\n' || password[password.size()-1]=='\r')) password.resize(password.size()-1);
  if(password.size()>=3 && static_cast<unsigned char>(password[0])==0xEF && static_cast<unsigned char>(password[1])==0xBB && static_cast<unsigned char>(password[2])==0xBF)
    password.erase(0,3);
  if(password.empty()) { err="password file first line is empty"; return false; }
  return true;
}

inline void oec_password_folder_preflight(int argc, const char* const* argv) {
  if(oec_has_explicit_auth(argc,argv)) return; // explicit CLI key wins
  const char* fk=std::getenv("FRANZKEY");
  if(fk && *fk) return; // existing environment key wins
  const char* pf=std::getenv("PASSWORD_FOLDER");
  if(!pf || !*pf) return;
  const std::string hint=oec_archive_hint_for_password(argc,argv);
  if(hint.empty()) return;
  const std::string stem=oec_password_stem_from_archive(hint);
  if(stem.empty()) return;
  std::string folder=pf;
  while(folder.size()>1 && (folder[folder.size()-1]=='/' || folder[folder.size()-1]=='\\')) folder.resize(folder.size()-1);
  const std::string path=folder + (folder.empty()?std::string():std::string("/")) + stem + ".password";
  if(!path_exists(path)) return; // no matching file: preserve normal interactive fallback
  std::string password, err;
  if(!oec_read_first_password_line(path,password,err)) {
    std::fprintf(stderr,"oec auth: PASSWORD_FOLDER match %s could not be used (%s); falling back to normal password handling\n",path.c_str(),err.c_str());
    return;
  }
  if(!oec_set_franzkey_env(password)) {
    std::fprintf(stderr,"oec auth: could not set FRANZKEY from %s; falling back to normal password handling\n",path.c_str());
    return;
  }
  std::fprintf(stderr,"oec auth: loaded archive password from %s\n",path.c_str());
}

inline std::string state_path(const std::string& base) { return base + ".ecstate"; }

inline bool read_state(const std::string& base, uint64_t& last) {
  FILE* f=std::fopen(state_path(base).c_str(), "rb");
  if (!f) return false;
  char magic[16]={0}; unsigned long long v=0;
  bool ok = std::fgets(magic,sizeof(magic),f) && (std::strncmp(magic,"OECST1",6)==0 || std::strncmp(magic,"ZFEXT1",6)==0) && std::fscanf(f,"last_part=%llu",&v)==1;
  std::fclose(f);
  if (ok) last=static_cast<uint64_t>(v);
  return ok;
}

inline bool write_state(const std::string& base, uint64_t last) {
  const std::string p=state_path(base), tmp=p+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb"); if(!f) return false;
  std::fprintf(f,"OECST1\nlast_part=%llu\n",(unsigned long long)last);
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
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
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


inline std::wstring oec_utf8_to_wide(const std::string& s) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  if (s.empty()) return std::wstring();
  int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,0,0);
  if(n<=0) return std::wstring();
  std::vector<wchar_t> w(static_cast<size_t>(n));
  MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,w.data(),n);
  return std::wstring(w.data());
#else
  (void)s; return std::wstring();
#endif
}

inline std::wstring oec_quote_win_arg(const std::wstring& a) {
  if (a.empty()) return L"\"\"";
  bool need=false;
  for(size_t i=0;i<a.size();++i) if(a[i]==L' '||a[i]==L'\t'||a[i]==L'\"') {need=true;break;}
  if(!need) return a;
  std::wstring r=L"\""; size_t bs=0;
  for(size_t i=0;i<a.size();++i){
    wchar_t c=a[i];
    if(c==L'\\'){ ++bs; continue; }
    if(c==L'\"'){ r.append(bs*2+1,L'\\'); r+=L'\"'; bs=0; continue; }
    r.append(bs,L'\\'); bs=0; r+=c;
  }
  r.append(bs*2,L'\\'); r+=L'\"'; return r;
}

inline bool oec_file_looks_standard_aes_encrypted(const std::string& path, bool& encrypted, std::string& err) {
  encrypted=false; err.clear();
  FILE* f=std::fopen(path.c_str(),"rb");
  if(!f){ err="cannot open archive/index for encryption probe: "+path; return false; }
  const int c=std::fgetc(f); std::fclose(f);
  if(c==EOF){ err="cannot probe empty archive/index: "+path; return false; }
  encrypted=(c!='z' && c!='7');
  return true;
}

inline std::vector<std::string> oec_extract_auth_args(const std::vector<std::string>& args) {
  std::vector<std::string> out;
  for(size_t i=0;i<args.size();++i) {
    if((args[i]=="-key" || args[i]=="-franzen") && i+1<args.size()) {
      out.push_back(args[i]); out.push_back(args[++i]);
    }
  }
  return out;
}

inline bool oec_is_password_prompt(const std::string& s) {
  return s.find("Enter AES password")!=std::string::npos
      || s.find("Enter password")!=std::string::npos
      || s.find("password:")!=std::string::npos;
}

inline bool oec_mask_auth_line(const std::string& line) {
  if(line.find("Archive is AES-encrypted")!=std::string::npos) return true;
  if(line.find("Enter AES password")!=std::string::npos) return true;
  if(line.find("Enter password")!=std::string::npos) return true;
  bool any=false;
  for(size_t i=0;i<line.size();++i) {
    const char c=line[i];
    if(c=='*'){ any=true; continue; }
    if(c==' '||c=='\t'||c=='\r'||c=='\n') continue;
    return false;
  }
  return any;
}

inline void oec_strip_auth_chatter(std::string& text) {
  std::string clean; clean.reserve(text.size());
  size_t pos=0;
  while(pos<text.size()) {
    size_t end=text.find('\n',pos);
    if(end==std::string::npos) end=text.size(); else ++end;
    const std::string line=text.substr(pos,end-pos);
    if(!oec_mask_auth_line(line)) clean+=line;
    pos=end;
  }
  text.swap(clean);
}

// Spawn this executable and capture stdout+stderr. Used only to materialize the
// disposable .idx cache from the authoritative .000 index.
inline int spawn_self_capture(const std::string& exe, const std::vector<std::string>& args,
                              std::string& output, std::string& err,
                              const char* stage=0) {
  output.clear(); err.clear();
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  SECURITY_ATTRIBUTES sa; std::memset(&sa,0,sizeof(sa)); sa.nLength=sizeof(sa); sa.bInheritHandle=TRUE;
  HANDLE rd=0, wr=0;
  if(!CreatePipe(&rd,&wr,&sa,0)){ err="CreatePipe failed"; return 127; }
  SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0);
  std::wstring cmd=oec_quote_win_arg(oec_utf8_to_wide(exe));
  for(size_t i=0;i<args.size();++i){ cmd+=L" "; cmd+=oec_quote_win_arg(oec_utf8_to_wide(args[i])); }
  std::vector<wchar_t> mutable_cmd(cmd.begin(),cmd.end()); mutable_cmd.push_back(0);
  STARTUPINFOW si; PROCESS_INFORMATION pi; std::memset(&si,0,sizeof(si)); std::memset(&pi,0,sizeof(pi));
  si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES; si.hStdOutput=wr; si.hStdError=wr; si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
  std::wstring wexe=oec_utf8_to_wide(exe);
  BOOL ok=CreateProcessW(wexe.empty()?0:wexe.c_str(), mutable_cmd.data(), 0,0,TRUE,CREATE_NO_WINDOW,0,0,&si,&pi);
  CloseHandle(wr);
  if(!ok){ CloseHandle(rd); err="CreateProcessW failed"; return 127; }
  char buf[16384]; DWORD got=0; bool auth_notice=false; std::string probe;
  while(ReadFile(rd,buf,sizeof(buf),&got,0) && got) {
    output.append(buf,buf+got);
    if(!auth_notice) {
      probe.append(buf,buf+got);
      if(probe.size()>1024) probe.erase(0,probe.size()-1024);
      if(oec_is_password_prompt(probe)) {
        std::fprintf(stderr,"\noec idx: %s requires an archive password; enter it now in this console. Set FRANZKEY, PASSWORD_FOLDER, or pass -key PASSWORD to avoid repeated prompts.\n", stage?stage:"native metadata pass");
        std::fflush(stderr); auth_notice=true;
      }
    }
  }
  CloseHandle(rd); WaitForSingleObject(pi.hProcess,INFINITE); DWORD code=127; GetExitCodeProcess(pi.hProcess,&code);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return static_cast<int>(code);
#else
  int pfd[2]; if(pipe(pfd)!=0){ err="pipe failed"; return 127; }
  pid_t pid=fork();
  if(pid<0){ close(pfd[0]); close(pfd[1]); err="fork failed"; return 127; }
  if(pid==0){
    dup2(pfd[1],STDOUT_FILENO); dup2(pfd[1],STDERR_FILENO); close(pfd[0]); close(pfd[1]);
    std::vector<char*> av; av.push_back(const_cast<char*>(exe.c_str()));
    for(size_t i=0;i<args.size();++i) av.push_back(const_cast<char*>(args[i].c_str())); av.push_back(0);
    execvp(exe.c_str(),av.data()); _exit(127);
  }
  close(pfd[1]); char buf[16384]; ssize_t n; bool auth_notice=false; std::string probe;
  while((n=read(pfd[0],buf,sizeof(buf)))>0) {
    output.append(buf,static_cast<size_t>(n));
    if(!auth_notice) {
      probe.append(buf,static_cast<size_t>(n));
      if(probe.size()>1024) probe.erase(0,probe.size()-1024);
      if(oec_is_password_prompt(probe)) {
        std::fprintf(stderr,"\noec idx: %s requires an archive password; enter it now in this console. Set FRANZKEY, PASSWORD_FOLDER, or pass -key PASSWORD to avoid repeated prompts.\n", stage?stage:"native metadata pass");
        std::fflush(stderr); auth_notice=true;
      }
    }
  }
  close(pfd[0]); int status=0; if(waitpid(pid,&status,0)<0){ err="waitpid failed"; return 127; }
  if(WIFEXITED(status)) return WEXITSTATUS(status); if(WIFSIGNALED(status)) return 128+WTERMSIG(status); return 127;
#endif
}


inline bool build_idx_cache(const std::string& exe, const std::string& index,
                            const std::string& idx_path, std::string& err,
                            const std::vector<std::string>& auth_args=std::vector<std::string>()) {
  if(!path_exists(index)){ err="zero-part index not found: "+index; return false; }
  std::string list_text, info_text, caperr;
  std::vector<std::string> a;
  a.push_back("l"); a.push_back(index); a.insert(a.end(),auth_args.begin(),auth_args.end());
  std::fprintf(stdout,"oec idx: stage 1/2 list metadata from %s ...\n",index.c_str()); std::fflush(stdout);
  int rc=spawn_self_capture(exe,a,list_text,caperr,"stage 1/2 native l");
  if(rc!=0){ std::ostringstream e; e<<"native l failed rc="<<rc; if(!caperr.empty())e<<" ("<<caperr<<")"; err=e.str(); return false; }
  oec_strip_auth_chatter(list_text);
  std::fprintf(stdout,"oec idx: stage 1/2 complete (%llu bytes cached)\n",(unsigned long long)list_text.size()); std::fflush(stdout);
  a.clear(); a.push_back("i"); a.push_back(index); a.insert(a.end(),auth_args.begin(),auth_args.end());
  std::fprintf(stdout,"oec idx: stage 2/2 version/info metadata from %s ...\n",index.c_str()); std::fflush(stdout);
  rc=spawn_self_capture(exe,a,info_text,caperr,"stage 2/2 native i");
  if(rc!=0){ std::ostringstream e; e<<"native i failed rc="<<rc; if(!caperr.empty())e<<" ("<<caperr<<")"; err=e.str(); return false; }
  oec_strip_auth_chatter(info_text);
  std::fprintf(stdout,"oec idx: stage 2/2 complete (%llu bytes cached)\n",(unsigned long long)info_text.size()); std::fflush(stdout);
  if(!oecidx::write_cache(idx_path,index,list_text,info_text,err)) return false;
  return true;
}

inline bool ensure_idx_cache(const std::string& exe, const std::string& index,
                             const std::string& idx_path, bool rebuild,
                             oecidx::Cache& cache, std::string& err,
                             const std::vector<std::string>& auth_args=std::vector<std::string>()) {
  if(cache.open(idx_path,index,err)) return true;
  if(!rebuild) return false;
  std::string why=err;
  if(!build_idx_cache(exe,index,idx_path,err,auth_args)) {
    if(!why.empty()) err="idx unavailable ("+why+"); rebuild failed: "+err;
    return false;
  }
  err.clear();
  if(!cache.open(idx_path,index,err)) return false;
  return true;
}

inline void oec_a_usage() {
  std::fprintf(stderr,
    "OEC (Optimize + Error Correction) add:\n"
    "  oec_a BASE_OR_SINGLE_ARCHIVE <zpaq add source/options...> [--ec-data N] [--ec-shard BYTES]\n"
    "           [--ec-stripes N] [--digits N] [--no-index-ec] [--no-part-ec]\n"
    "           [--idx PATH] [--idx-refresh] [--idx-plaintext] [--no-idx]\n\n"
    "Examples:\n"
    "  zpaqoec oec_a compress /data -method 5\n"
    "  zpaqoec oec_a archive.zpaq /data -method 5\n\n"
    "Multipart mode creates a new BASE.NNN. Single-file mode appends to the\n"
    "existing archive and regenerates its sidecar EC. Both use a .000 metadata index.\n");
}

inline int oec_a(int argc, const char* const* argv) {
  if (argc < 3) { oec_a_usage(); return 2; }
  const std::string exe = argv[0];
  const std::string base = argv[2];
  const bool single = base.find('?')==std::string::npos && path_exists(base);
  uint32_t digits=3;
  zfec::Options ecopt;
  bool protect_trunk=true, protect_part=true;
  bool use_idx=true, refresh_idx=false, idx_explicit=false, idx_plaintext=false;
  std::string idx_path = single ? infer_single_idx(base) : base + ".idx";
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
    } else if ((a=="--no-index-ec" || a=="--no-trunk-ec")) protect_trunk=false;
    else if (a=="--no-part-ec") protect_part=false;
    else if (a=="--idx" && i+1<argc) { idx_path=argv[++i]; use_idx=true; idx_explicit=true; }
    else if (a=="--idx-refresh") { refresh_idx=true; use_idx=true; }
    else if (a=="--idx-plaintext") { idx_plaintext=true; use_idx=true; }
    else if (a=="--no-idx") use_idx=false;
    else if (a=="-index" || a=="--index" || a=="--oec-index") {
      std::fprintf(stderr,"oec_a owns the OEC zero-part index path; initialize the archive with oecinit first\n"); return 2;
    } else pass.push_back(a);
  }
  (void)idx_explicit;
  std::string verr;
  if (!zfec::validate_options(ecopt, verr)) { std::fprintf(stderr,"bad EC options: %s\n",verr.c_str()); return 2; }
  if (pass.empty()) { std::fprintf(stderr,"oec_a: missing source/add arguments\n"); return 2; }

  std::string data_target;
  std::string index;
  std::string expected_part;
  uint64_t next=0;
  if(single) {
    data_target=base;
    index=infer_single_index(base);
    expected_part=base;
    if(!path_exists(index)) {
      std::fprintf(stderr,"oec_a: single archive zero-part index not found: %s (run oecinit/oec_init first)\n",index.c_str());
      return 3;
    }
    std::fprintf(stdout,"oec_a: mode=single data=%s index=%s\n",data_target.c_str(),index.c_str());
  } else {
    data_target = base + "." + qmarks(digits);
    index = base + "." + zero_suffix(0,digits);
    uint64_t maxn=1;
    for (uint32_t i=0;i<digits;++i) maxn*=10;
    maxn-=1;
    uint64_t last=0;
    if (!read_state(base,last)) {
      if (path_exists(index)) {
        last=recover_last_part_by_names(base,digits,maxn);
        std::fprintf(stdout,"oec_a: recovered missing .ecstate once (last_part=%llu)\n",(unsigned long long)last);
      }
    }
    next=last+1;
    if (next>maxn) { std::fprintf(stderr,"oec_a: part number space exhausted\n"); return 1; }
    expected_part = base + "." + zero_suffix(next,digits);
    std::fprintf(stdout,"oec_a: mode=multipart index=%s next=%s\n", index.c_str(), expected_part.c_str());
  }

  std::vector<std::string> child;
  child.push_back("a"); child.push_back(data_target);
  child.insert(child.end(), pass.begin(), pass.end());
  child.push_back("-index"); child.push_back(index);

  const int rc=spawn_self(exe, child);
  if (rc!=0) { std::fprintf(stderr,"oec_a: zpaq add failed rc=%d; EC not written\n",rc); return rc; }
  if (!path_exists(expected_part)) {
    std::fprintf(stderr,"oec_a: add succeeded but expected data file %s was not found; refusing to guess\n",expected_part.c_str());
    return 4;
  }
  if (!single && !write_state(base,next)) {
    std::fprintf(stderr,"oec_a: part was added but could not update %s; next run will recover once from filenames\n",state_path(base).c_str());
  }

  std::string err;
  if (protect_part) {
    if (!zfec::create(expected_part, zfec::default_ec_path(expected_part), ecopt, true, err)) {
      std::fprintf(stderr,"oec_a: archive data is valid but EC creation failed: %s\n",err.c_str()); return 5;
    }
    std::fprintf(stdout,"oec_a: protected %s -> %s.ec\n",expected_part.c_str(),expected_part.c_str());
  }
  if (protect_trunk && path_exists(index)) {
    if (!zfec::create(index, zfec::default_ec_path(index), ecopt, true, err)) {
      std::fprintf(stderr,"oec_a: data EC OK, zero-part EC failed: %s\n",err.c_str()); return 6;
    }
    std::fprintf(stdout,"oec_a: protected zero part %s -> %s.ec\n",index.c_str(),index.c_str());
  }
  if (use_idx) {
    if (refresh_idx) {
      bool enc=false; std::string encerr;
      if(oec_file_looks_standard_aes_encrypted(index,enc,encerr) && enc && !idx_plaintext) {
        std::fprintf(stdout,"oec_a: encrypted zero-part detected; plaintext idx refresh skipped (use --idx-plaintext to opt in)\n");
      } else {
        const std::vector<std::string> auth=oec_extract_auth_args(pass);
        if (!build_idx_cache(exe,index,idx_path,err,auth)) {
          std::fprintf(stderr,"oec_a: archive update OK, idx refresh failed: %s\n",err.c_str()); return 7;
        }
        std::fprintf(stdout,"oec_a: refreshed idx %s\n",idx_path.c_str());
      }
    } else {
      if (path_exists(idx_path))
        std::fprintf(stdout,"oec_a: idx %s is now stale by source fingerprint; next OEC metadata read will rebuild it lazily (use --idx-refresh to refresh now)\n",idx_path.c_str());
      else
        std::fprintf(stdout,"oec_a: idx %s is not present; next OEC metadata read will build it lazily (use --idx-refresh to build now)\n",idx_path.c_str());
    }
  }
  return 0;
}


inline void oecinit_usage() {
  std::fprintf(stderr,
    "Initialize/retrofit OEC archive (Optimize + Error Correction):\n"
    "  oecinit ARCHIVE_OR_PATTERN [-index INDEX] [--force]\n"
    "            [--ec-data N] [--ec-shard BYTES] [--ec-stripes N]\n"
    "            [--no-index-ec] [--no-part-ec] [--idx PATH] [--idx-plaintext] [--no-idx]\n"
    "            [zpaq read options...]\n\n"
    "Examples:\n"
    "  zpaqoec oecinit \"compress.???\"\n"
    "  zpaqoec oecinit archive.zpaq\n"
    "  zpaqoec oecinit \"backup_????????.zpaq\" -index backup_00000000.zpaq\n"
    "  zpaqoec oecinit \"secret.???\" -key PASSWORD\n\n"
    "For multipart archives the index replaces ? with 0. For a single archive\n"
    "foo.zpaq, the zero part is foo.000.zpaq and EC is foo.zpaq.ec.\n"
    "Index generation uses the native ZPAQ extract -index path, so archive data\n"
    "are never rewritten. EC sidecars are generated independently for every part.\n");
}

inline bool replace_file_safely(const std::string& tmp, const std::string& dst, std::string& err) {
  const std::string bak = dst + ".oecinit.bak";
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

inline int oecinit(int argc, const char* const* argv) {
  if (argc < 3) { oecinit_usage(); return 2; }
  const std::string exe = argv[0];
  const std::string requested = argv[2];
  const bool exact_single = requested.find('?') == std::string::npos && path_exists(requested);

  std::string pattern;
  PatternParts pp;
  std::string perr;
  std::string single_data;
  std::string index;
  std::string idx_path;
  if (exact_single) {
    single_data=requested;
    index=infer_single_index(single_data);
    idx_path=infer_single_idx(single_data);
  } else {
    pattern=requested;
    if (pattern.find('?') == std::string::npos) pattern += ".???";
    if (!parse_qmark_pattern(pattern, pp, perr)) { std::fprintf(stderr, "oecinit: %s\n", perr.c_str()); return 2; }
    index = infer_index_from_pattern(pp);
    if (pp.suffix.empty() && !pp.prefix.empty() && pp.prefix[pp.prefix.size()-1]=='.')
      idx_path = pp.prefix.substr(0,pp.prefix.size()-1) + ".idx";
    else idx_path = index + ".idx";
  }

  bool force = false, protect_trunk = true, protect_parts = true, use_idx = true, idx_plaintext=false;
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
    } else if ((a=="--no-index-ec" || a=="--no-trunk-ec")) protect_trunk = false;
    else if (a=="--no-part-ec") protect_parts = false;
    else if (a=="--idx" && i+1<argc) { idx_path=argv[++i]; use_idx=true; }
    else if (a=="--no-idx") use_idx=false;
    else if (a=="--idx-plaintext") { use_idx=true; idx_plaintext=true; }
    else readopts.push_back(a);
  }

  std::string verr;
  if (!zfec::validate_options(ecopt, verr)) { std::fprintf(stderr,"bad EC options: %s\n",verr.c_str()); return 2; }

  uint64_t last = 0, created = 0, skipped = 0;
  std::string err;

  // Single-file compatibility mode: protect the original bytes first. This is
  // deliberately ordered before zero-index creation so EC remains available
  // even if the native metadata-index pass cannot be completed.
  if (exact_single) {
    std::fprintf(stdout,"oecinit: mode=single data=%s index=%s\n",single_data.c_str(),index.c_str());
    last=1;
    if (protect_parts) {
      const std::string ec=zfec::default_ec_path(single_data);
      if(path_exists(ec) && !force) {
        ++skipped;
        std::fprintf(stdout,"oecinit: EC exists, skip %s\n",ec.c_str());
      } else if(!zfec::create(single_data,ec,ecopt,true,err)) {
        std::fprintf(stderr,"oecinit: single archive EC creation failed for %s: %s\n",single_data.c_str(),err.c_str());
        return 5;
      } else {
        ++created;
        std::fprintf(stdout,"oecinit: protected single archive %s -> %s\n",single_data.c_str(),ec.c_str());
      }
    }

    const bool have_index=path_exists(index);
    if(have_index && !force) {
      std::fprintf(stdout,"oecinit: reusing existing zero-part index %s (use --force to rebuild)\n",index.c_str());
    } else {
      const std::string tmpindex=index+".oecinit.tmp";
      std::remove(tmpindex.c_str());
      std::vector<std::string> child;
      child.push_back("x"); child.push_back(single_data);
      child.insert(child.end(),readopts.begin(),readopts.end());
      child.push_back("-index"); child.push_back(tmpindex); child.push_back("-force");
      std::fprintf(stdout,"oecinit: rebuilding zero-part index %s from single archive %s\n",index.c_str(),single_data.c_str());
      const int rc=spawn_self(exe,child);
      if(rc!=0) {
        std::remove(tmpindex.c_str());
        std::fprintf(stderr,"oecinit: single archive EC is ready, but zero-part index rebuild failed rc=%d; original archive was not modified\n",rc);
        return rc;
      }
      if(!path_exists(tmpindex)) {
        std::fprintf(stderr,"oecinit: single archive EC is ready, but native index rebuild did not create %s\n",tmpindex.c_str());
        return 4;
      }
      if(!replace_file_safely(tmpindex,index,verr)) {
        std::fprintf(stderr,"oecinit: single archive EC is ready, but %s\n",verr.c_str());
        return 4;
      }
    }
  } else {
    const std::string first = pattern_number(pp, 1);
    if (!path_exists(first)) { std::fprintf(stderr,"oecinit: first archive part not found: %s\n",first.c_str()); return 3; }
    const bool have_index = path_exists(index);
    if (have_index && !force) {
      std::fprintf(stdout,"oecinit: reusing existing zero-part index %s (use --force to rebuild)\n",index.c_str());
    } else {
      // Build to a temporary path. Native `x -index` reads archive metadata but does not extract files.
      const std::string tmpindex = index + ".oecinit.tmp";
      std::remove(tmpindex.c_str());
      std::vector<std::string> child;
      child.push_back("x"); child.push_back(pattern);
      child.insert(child.end(), readopts.begin(), readopts.end());
      child.push_back("-index"); child.push_back(tmpindex);
      child.push_back("-force");
      std::fprintf(stdout,"oecinit: rebuilding index %s from %s\n",index.c_str(),pattern.c_str());
      const int rc = spawn_self(exe, child);
      if (rc != 0) {
        std::remove(tmpindex.c_str());
        std::fprintf(stderr,"oecinit: native index rebuild failed rc=%d; archive parts were not modified\n",rc);
        return rc;
      }
      if (!path_exists(tmpindex)) {
        std::fprintf(stderr,"oecinit: native index rebuild returned success but did not create %s\n",tmpindex.c_str());
        return 4;
      }
      if (!replace_file_safely(tmpindex, index, verr)) { std::fprintf(stderr,"oecinit: %s\n",verr.c_str()); return 4; }
    }

    uint64_t maxn = 1;
    for (uint32_t i=0;i<pp.digits;++i) maxn *= 10;
    maxn -= 1;
    if (protect_parts) {
      for (uint64_t n=1;n<=maxn;++n) {
        const std::string part = pattern_number(pp,n);
        if (!path_exists(part)) { last = n-1; break; }
        last = n;
        const std::string ec = zfec::default_ec_path(part);
        if (path_exists(ec) && !force) {
          ++skipped;
          std::fprintf(stdout,"oecinit: EC exists, skip %s\n",ec.c_str());
          continue;
        }
        if (!zfec::create(part, ec, ecopt, true, err)) {
          std::fprintf(stderr,"oecinit: index is ready, but EC creation failed for %s: %s\n",part.c_str(),err.c_str());
          return 5;
        }
        ++created;
        std::fprintf(stdout,"oecinit: protected %s -> %s\n",part.c_str(),ec.c_str());
      }
    } else {
      for (uint64_t n=1;n<=maxn;++n) {
        const std::string part = pattern_number(pp,n);
        if (!path_exists(part)) { last=n-1; break; }
        last=n;
      }
    }
    if (last == 0) { std::fprintf(stderr,"oecinit: no archive parts found after index rebuild\n"); return 4; }
  }

  if (protect_trunk) {
    const std::string ec = zfec::default_ec_path(index);
    if (path_exists(ec) && !force) {
      ++skipped;
      std::fprintf(stdout,"oecinit: zero-part EC exists, skip %s\n",ec.c_str());
    } else if (!zfec::create(index, ec, ecopt, true, err)) {
      std::fprintf(stderr,"oecinit: data EC ready, zero-part EC failed: %s\n",err.c_str());
      return 6;
    } else {
      ++created;
      std::fprintf(stdout,"oecinit: protected zero part %s -> %s\n",index.c_str(),ec.c_str());
    }
  }

  // Multipart BASE.??? archives use ecstate for next-part allocation. A legacy
  // single-file archive keeps appending to the same file and does not need it.
  if (!exact_single && pp.suffix.empty() && pp.prefix.size() >= 1 && pp.prefix[pp.prefix.size()-1] == '.') {
    const std::string base = pp.prefix.substr(0, pp.prefix.size()-1);
    if (!base.empty() && !write_state(base,last))
      std::fprintf(stderr,"oecinit: warning: could not write %s\n",state_path(base).c_str());
  }
  bool idx_security_skip=false;
  if (use_idx) {
    bool enc=false; std::string encerr;
    if(oec_file_looks_standard_aes_encrypted(index,enc,encerr) && enc && !idx_plaintext) {
      idx_security_skip=true;
      std::fprintf(stdout,"oecinit: encrypted zero-part detected; plaintext idx cache is disabled by default.\n");
      if(path_exists(idx_path)) std::fprintf(stdout,"oecinit: existing idx %s will not be used automatically for this encrypted archive; use oec_idx drop to remove it if it came from an older OEC build.\n",idx_path.c_str());
      std::fprintf(stdout,"oecinit: use --idx-plaintext to opt in, preferably with FRANZKEY or -key PASSWORD so native l/i metadata passes do not prompt repeatedly.\n");
    } else {
      oecidx::Cache cache;
      std::string idxerr;
      if (!force && cache.open(idx_path,index,idxerr)) {
        std::fprintf(stdout,"oecinit: reusing valid mmap cache %s\n",idx_path.c_str());
      } else {
        if (!idxerr.empty() && path_exists(idx_path))
          std::fprintf(stdout,"oecinit: rebuilding stale/invalid idx %s (%s)\n",idx_path.c_str(),idxerr.c_str());
        const std::vector<std::string> auth=oec_extract_auth_args(readopts);
        std::fprintf(stdout,"oecinit: building mmap idx %s from zero-part metadata\n",idx_path.c_str()); std::fflush(stdout);
        if (!build_idx_cache(exe,index,idx_path,err,auth)) {
          std::fprintf(stderr,"oecinit: archive/index/EC ready, but idx build failed: %s\n",err.c_str());
          return 7;
        }
        std::fprintf(stdout,"oecinit: built mmap cache %s\n",idx_path.c_str());
      }
    }
  }
  const std::string idx_status=!use_idx?"disabled":(idx_security_skip?"skipped-encrypted-plaintext-policy":idx_path);
  std::fprintf(stdout,"oecinit: DONE mode=%s parts=%llu ec_created=%llu ec_skipped=%llu index=%s idx=%s\n",
    exact_single?"single":"multipart",(unsigned long long)last,(unsigned long long)created,(unsigned long long)skipped,index.c_str(),idx_status.c_str());
  return 0;
}



// OEC read-command routing. The portable .000 index is authoritative metadata
// and contains no D blocks. Therefore metadata-only commands can run directly
// against .000, while extraction commands must still address the multipart
// data pattern. The mmap .idx accelerator serves default metadata reads now;
// deeper fragment->part and dedup backends remain guarded/fallback integrations.
struct OecReadLayout {
  std::string pattern; // multipart pattern or exact single-file payload path
  std::string index;
  uint32_t digits = 3;
  bool single = false;
};

inline bool resolve_oec_read_layout(const std::string& spec, uint32_t digits,
                                    const std::string& index_override,
                                    OecReadLayout& out, std::string& err) {
  if (digits < 1 || digits > 9) { err = "digit count must be 1..9"; return false; }
  out.digits = digits;
  out.single = false;
  if (spec.find('?') != std::string::npos) {
    PatternParts pp;
    if (!parse_qmark_pattern(spec, pp, err)) return false;
    out.pattern = spec;
    out.digits = pp.digits;
    out.index = index_override.empty() ? infer_index_from_pattern(pp) : index_override;
    return true;
  }
  // An exact existing file is a legacy single-part archive. A non-existing
  // bare name retains the OEC multipart shorthand BASE -> BASE.???.
  if (path_exists(spec)) {
    out.single = true;
    out.pattern = spec;
    out.index = index_override.empty() ? infer_single_index(spec) : index_override;
    return true;
  }
  out.pattern = spec + "." + qmarks(digits);
  out.index = index_override.empty() ? spec + "." + zero_suffix(0, digits) : index_override;
  return true;
}


inline std::string default_idx_for_layout(const std::string& spec, const OecReadLayout& layout) {
  if (layout.single) return infer_single_idx(spec);
  if (spec.find('?') == std::string::npos) return spec + ".idx";
  PatternParts pp; std::string err;
  if (parse_qmark_pattern(spec,pp,err) && pp.suffix.empty() && !pp.prefix.empty() && pp.prefix[pp.prefix.size()-1]=='.')
    return pp.prefix.substr(0,pp.prefix.size()-1) + ".idx";
  return layout.index + ".idx";
}

inline int oec_idx_command(int argc, const char* const* argv) {
  if (argc < 4) {
    std::fprintf(stderr,
      "OEC mmap cache manager:\n"
      "  oec_idx build|verify|info|drop ARCHIVE [--idx PATH] [--digits N] [--oec-index PATH] [--idx-plaintext]\n"
      "Examples:\n"
      "  zpaqoec oec_idx build compress --idx X:/FastCache/compress.idx\n"
      "  zpaqoec oec_idx verify compress --idx X:/FastCache/compress.idx\n");
    return 2;
  }
  const std::string exe=argv[0], action=argv[2], spec=argv[3];
  uint32_t digits=3; std::string index_override, idx_override; bool idx_plaintext=false; std::vector<std::string> idx_auth;
  for(int i=4;i<argc;++i){
    const std::string a=argv[i];
    if(a=="--digits" && i+1<argc){ if(!zfec::parse_u32(argv[++i],digits)||digits<1||digits>9){std::fprintf(stderr,"oec_idx: bad --digits\n");return 2;} }
    else if(a=="--oec-index" && i+1<argc) index_override=argv[++i];
    else if(a=="--idx" && i+1<argc) idx_override=argv[++i];
    else if(a=="--idx-plaintext") idx_plaintext=true;
    else if((a=="-key" || a=="-franzen") && i+1<argc) { idx_auth.push_back(a); idx_auth.push_back(argv[++i]); }
    else { std::fprintf(stderr,"oec_idx: unknown option %s\n",a.c_str()); return 2; }
  }
  OecReadLayout layout; std::string err;
  if(!resolve_oec_read_layout(spec,digits,index_override,layout,err)){std::fprintf(stderr,"oec_idx: %s\n",err.c_str());return 2;}
  const std::string idx=idx_override.empty()?default_idx_for_layout(spec,layout):idx_override;
  if(action=="build"){
    bool enc=false; std::string encerr;
    if(oec_file_looks_standard_aes_encrypted(layout.index,enc,encerr) && enc && !idx_plaintext) {
      std::fprintf(stderr,"oec_idx: encrypted zero-part detected; refusing to create plaintext metadata cache without --idx-plaintext\n"); return 3;
    }
    if(!build_idx_cache(exe,layout.index,idx,err,idx_auth)){std::fprintf(stderr,"oec_idx: build failed: %s\n",err.c_str());return 4;}
    oecidx::Cache c; if(!c.open(idx,layout.index,err)){std::fprintf(stderr,"oec_idx: post-build verify failed: %s\n",err.c_str());return 4;}
    std::fprintf(stdout,"oec_idx: built %s\n%s\n",idx.c_str(),oecidx::describe(*c.header()).c_str()); return 0;
  }
  if(action=="drop"){
    if(!oecidx::remove_cache(idx,err)){std::fprintf(stderr,"oec_idx: %s\n",err.c_str());return 4;}
    std::fprintf(stdout,"oec_idx: dropped %s\n",idx.c_str());return 0;
  }
  oecidx::Cache c;
  if(!c.open(idx,layout.index,err)){std::fprintf(stderr,"oec_idx: %s: %s\n",action.c_str(),err.c_str());return 3;}
  if(action=="verify") { std::fprintf(stdout,"oec_idx: OK %s\n%s\n",idx.c_str(),oecidx::describe(*c.header()).c_str()); return 0; }
  if(action=="info") { std::fprintf(stdout,"idx=%s\nsource=%s\n%s\n",idx.c_str(),layout.index.c_str(),oecidx::describe(*c.header()).c_str()); return 0; }
  std::fprintf(stderr,"oec_idx: unknown action %s\n",action.c_str()); return 2;
}

inline void oec_read_usage(const char* cmd) {
  const bool metadata = std::strcmp(cmd, "oec_l") == 0 || std::strcmp(cmd, "oec_i") == 0;
  std::fprintf(stderr,
    "OEC optimized %s:\n"
    "  %s ARCHIVE [native options/files...] [--digits N] [--oec-index PATH]\n"
    "       [--idx PATH] [--no-idx] [--idx-no-rebuild] [--idx-plaintext]\n\n"
    "Examples:\n"
    "  zpaqoec %s compress -all\n"
    "  zpaqoec %s \"backup_????????.zpaq\"\n\n"
    "%s\n",
    metadata ? "metadata command" : "extract command", cmd, cmd, cmd,
    metadata
      ? "Metadata-only OEC commands read the zero-part index and do not touch data parts."
      : "Extraction uses the multipart data pattern because the zero-part index has no D blocks.");
}

inline int oec_read_command(int argc, const char* const* argv,
                            const char* oec_cmd, const char* native_cmd,
                            bool metadata_only) {
  if (argc < 3) { oec_read_usage(oec_cmd); return 2; }
  const std::string exe = argv[0];
  const std::string spec = argv[2];
  uint32_t digits = 3;
  std::string index_override, idx_override;
  bool use_idx=true, rebuild_idx=true, idx_plaintext=false, semantic_opts=false;
  std::vector<std::string> pass, idx_auth;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--digits" && i + 1 < argc) {
      if (!zfec::parse_u32(argv[++i], digits) || digits < 1 || digits > 9) {
        std::fprintf(stderr, "%s: bad --digits\n", oec_cmd); return 2;
      }
    } else if (a == "--oec-index" && i + 1 < argc) {
      index_override = argv[++i];
    } else if (a == "--idx" && i + 1 < argc) {
      idx_override = argv[++i]; use_idx=true;
    } else if (a == "--no-idx") {
      use_idx=false;
    } else if (a == "--idx-no-rebuild") {
      rebuild_idx=false;
    } else if (a == "--idx-plaintext") {
      idx_plaintext=true; use_idx=true;
    } else if ((a == "-key" || a == "-franzen") && i + 1 < argc) {
      const std::string v=argv[++i]; idx_auth.push_back(a); idx_auth.push_back(v); pass.push_back(a); pass.push_back(v);
    } else if (a == "-index" || a == "--index") {
      std::fprintf(stderr,
        "%s: -index is reserved by native ZPAQ semantics; use --oec-index PATH to select the OEC zero part\n",
        oec_cmd);
      return 2;
    } else {
      pass.push_back(a); semantic_opts=true;
    }
  }

  OecReadLayout layout;
  std::string err;
  if (!resolve_oec_read_layout(spec, digits, index_override, layout, err)) {
    std::fprintf(stderr, "%s: %s\n", oec_cmd, err.c_str()); return 2;
  }
  const std::string idx_path=idx_override.empty()?default_idx_for_layout(spec,layout):idx_override;

  const std::string target = metadata_only ? layout.index : layout.pattern;
  if (metadata_only) {
    if (!path_exists(layout.index)) {
      std::fprintf(stderr, "%s: OEC zero-part index not found: %s (run oecinit first)\n",
                   oec_cmd, layout.index.c_str());
      return 3;
    }

    // The default l/i forms are fully cacheable. Native filtering/search/options
    // retain exact upstream semantics and therefore fall back to .000 parsing.
    bool encidx=false; std::string encprobe;
    const bool enc_known=oec_file_looks_standard_aes_encrypted(layout.index,encidx,encprobe);
    if(use_idx && enc_known && encidx && !idx_plaintext) {
      std::fprintf(stdout,"%s: encrypted zero-part; plaintext idx disabled (use --idx-plaintext to opt in), falling back to native zero-part parser\n",oec_cmd);
    } else if(use_idx && !semantic_opts) {
      oecidx::Cache cache;
      if(ensure_idx_cache(exe,layout.index,idx_path,rebuild_idx,cache,err,idx_auth)) {
        const bool want_list = std::strcmp(native_cmd,"l")==0;
        const char* p = want_list ? cache.list_data() : cache.info_data();
        const size_t n = want_list ? cache.list_size() : cache.info_size();
        if(p && n) {
          std::fprintf(stdout,"%s: idx=%s (mmap) source=%s\n",oec_cmd,idx_path.c_str(),layout.index.c_str());
          if(std::fwrite(p,1,n,stdout)!=n) return 5;
          return 0;
        }
      } else {
        std::fprintf(stderr,"%s: idx unavailable (%s); falling back to %s\n",oec_cmd,err.c_str(),layout.index.c_str());
      }
    } else if(use_idx && semantic_opts) {
      // Validate an existing cache but do not rebuild just to execute a native
      // option variant. This keeps option-rich list/info exact and cheap.
      oecidx::Cache cache;
      if(cache.open(idx_path,layout.index,err))
        std::fprintf(stdout,"%s: idx=%s valid; native options require zero-part parser\n",oec_cmd,idx_path.c_str());
    }
    std::fprintf(stdout, "%s: metadata=%s\n", oec_cmd, layout.index.c_str());
  } else {
    if (layout.single) {
      if (!path_exists(layout.pattern)) {
        std::fprintf(stderr, "%s: single data archive not found: %s\n", oec_cmd, layout.pattern.c_str());
        return 3;
      }
    } else {
      PatternParts pp;
      if (!parse_qmark_pattern(layout.pattern, pp, err)) {
        std::fprintf(stderr, "%s: %s\n", oec_cmd, err.c_str()); return 2;
      }
      const std::string first = pattern_number(pp, 1);
      if (!path_exists(first)) {
        std::fprintf(stderr, "%s: first data part not found: %s\n", oec_cmd, first.c_str());
        return 3;
      }
    }
    if (!path_exists(layout.index)) {
      std::fprintf(stderr, "%s: OEC zero-part index not found: %s (run oecinit first)\n",
                   oec_cmd, layout.index.c_str());
      return 3;
    }
    // Extraction still delegates payload decoding to upstream. An existing idx
    // is mmap-validated here and is ready for the deeper fragment->part locator
    // backend; no cache rebuild is forced on the extraction hot path.
    if(use_idx) {
      oecidx::Cache cache;
      if(cache.open(idx_path,layout.index,err))
        std::fprintf(stdout,"%s: idx=%s valid (metadata accelerator), data=%s\n",oec_cmd,idx_path.c_str(),layout.pattern.c_str());
      else
        std::fprintf(stdout,"%s: idx unavailable (%s), data=%s\n",oec_cmd,err.c_str(),layout.pattern.c_str());
    } else {
      std::fprintf(stdout, "%s: metadata=%s data=%s\n", oec_cmd, layout.index.c_str(), layout.pattern.c_str());
    }
  }

  std::vector<std::string> child;
  child.push_back(native_cmd);
  child.push_back(target);
  child.insert(child.end(), pass.begin(), pass.end());
  return spawn_self(exe, child);
}

inline int oec_l(int argc, const char* const* argv) { return oec_read_command(argc, argv, "oec_l", "l", true); }
inline int oec_i(int argc, const char* const* argv) { return oec_read_command(argc, argv, "oec_i", "i", true); }
inline int oec_x(int argc, const char* const* argv) { return oec_read_command(argc, argv, "oec_x", "x", false); }
inline int oec_e(int argc, const char* const* argv) { return oec_read_command(argc, argv, "oec_e", "e", false); }

inline void oec_quick_help(const char* exe) {
  const char* p = (exe && *exe) ? exe : "zpaqoec";
  std::fprintf(stdout,
    "zpaqoec - OEC (Optimize + Error Correction) for zpaqfranz\n"
    "\n"
    "OEC commands:\n"
    "  oecinit | oec_init ARCHIVE     initialize/retrofit single or multipart .000 + EC\n"
    "  oec_a BASE SOURCE...            optimized incremental add using .000 + EC\n"
    "  oec_l ARCHIVE [options...]      optimized list; metadata from .000 only\n"
    "  oec_i ARCHIVE [options...]      optimized info/versions; metadata from .000 only\n"
    "  oec_x ARCHIVE [files/options]   OEC equivalent of native x\n"
    "  oec_e ARCHIVE [files/options]   OEC equivalent of native e\n"
    "  oec_idx build|verify|info|drop  mmap SSD cache manager\n"
    "  ec create|verify|repair|info    independent EC sidecar operations\n"
    "  oec_version                    show OEC overlay version/build identity\n"
    "\n"
    "Examples:\n"
    "  %s oecinit \"backup.???\"\n"
    "  %s oecinit backup.zpaq\n"
    "  %s oec_a backup /data -method 5\n"
    "  %s oec_l backup -all\n"
    "  %s oec_x backup path/to/file -to restore\n"
    "\n"
    "Original zpaqfranz commands remain available unchanged (a, l, i, x, e, ...).\n"
    "Password lookup: if PASSWORD_FOLDER is set and no -key/FRANZKEY is present, OEC tries <archive>.password there before interactive input.\n"
    "Use '%s h h' for full upstream help. See docs/OEC_COMMANDS.md for OEC details.\n",
    p, p, p, p, p, p);
}

inline int dispatch_const(int argc, const char* const* argv) {
  if (!argv) return kNotHandled;
  oec_password_folder_preflight(argc, argv);
  if (argc < 2 || !argv[1]) { oec_quick_help(argc > 0 ? argv[0] : 0); return 0; }
  const std::string cmd=argv[1];
  if (cmd=="oec_help" || cmd=="oec_h") { oec_quick_help(argv[0]); return 0; }
  if (cmd=="oec_version") { std::fprintf(stdout, "zpaqoec OEC overlay %s (Optimize + Error Correction)\n", kOecOverlayVersion); return 0; }
  if (cmd=="ec") return zfec::cli(argc-1, argv+1);
  if (cmd=="oec_idx") return oec_idx_command(argc, argv);
  if (cmd=="oec_a") return oec_a(argc, argv);
  if (cmd=="oecinit" || cmd=="oec_init") return oecinit(argc, argv);
  if (cmd=="oec_l") return oec_l(argc, argv);
  if (cmd=="oec_i") return oec_i(argc, argv);
  if (cmd=="oec_x") return oec_x(argc, argv);
  if (cmd=="oec_e") return oec_e(argc, argv);
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
