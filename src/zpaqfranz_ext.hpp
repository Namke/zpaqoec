#pragma once

#include "zfec.hpp"
#include "oec_idx.hpp"
#include "oec_md5.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <ctime>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  // Windows CRT directory/file APIs used by OEC source walking and temp cleanup.
  // MinGW UCRT does not guarantee that zpaqfranz upstream includes these before
  // this extension, so include them explicitly here.
  #include <io.h>
  #include <sys/stat.h>
  #include <process.h>
  #include <direct.h>
#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #include <dirent.h>
#endif

namespace zfext {

static const int kNotHandled = -777777;
static const char* const kOecOverlayVersion = "0.3.8";

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
  if(cmd=="oecinit" || cmd=="oec_init" || cmd=="oec_a" || cmd=="oec_l" || cmd=="oec_i" || cmd=="oec_x" || cmd=="oec_e" || cmd=="oec_json" || cmd=="oec_j")
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

struct OecIgnoreRule {
  std::string pattern; bool negated; bool dir_only; bool has_slash; bool anchored;
  OecIgnoreRule():negated(false),dir_only(false),has_slash(false),anchored(false){}
};
struct OecIgnoreFile { std::string source_root; std::string rel; std::string full; };
struct OecIgnorePlan {
  bool active; bool gitignore_enabled; uint64_t scanned_files; uint64_t ignored_files; uint64_t ignored_dirs;
  std::vector<std::string> rule_files; std::vector<std::string> native_excludes; std::vector<OecIgnoreFile> allowed_files;
  OecIgnorePlan():active(false),gitignore_enabled(false),scanned_files(0),ignored_files(0),ignored_dirs(0){}
};
inline bool oec_prepare_ignore_plan(const std::vector<std::string>& add_args,bool use_gitignore,OecIgnorePlan& plan,std::string& err);
inline bool oec_write_ignore_exclude_file(const OecIgnorePlan& plan,std::string& path,std::string& err);

inline bool oec_progressive_json_after_add(const std::string& exe,const std::string& archive_spec,const std::string& index,
                                           const std::vector<std::string>& add_args,bool force_create,std::string& err,const OecIgnorePlan* ignore_plan=0);

inline void oec_a_usage() {
  std::fprintf(stderr,
    "OEC (Optimize + Error Correction) add:\n"
    "  oec_a BASE_OR_SINGLE_ARCHIVE <zpaq add source/options...> [--ec-data N] [--ec-shard BYTES]\n"
    "           [--ec-stripes N] [--digits N] [--no-index-ec] [--no-part-ec]\n"
    "           [--idx PATH] [--idx-refresh] [--idx-plaintext] [--no-idx]\n"
    "           [--json-force|--force-json] [-gitignore]\n\n"
    "Examples:\n"
    "  zpaqoec oec_a compress /data -method 5\n"
    "  zpaqoec oec_a archive.zpaq /data -method 5\n\n"
    "zpaq.ignore is loaded automatically from each source folder (gitignore-style);\n"
    "-gitignore also loads .gitignore from each source folder.\n"
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
  bool use_idx=true, refresh_idx=false, idx_explicit=false, idx_plaintext=false, json_force=false, use_gitignore=false;
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
    else if (a=="--json-force" || a=="--force-json") json_force=true;
    else if (a=="-gitignore" || a=="--gitignore") use_gitignore=true;
    else if (a=="-index" || a=="--index" || a=="--oec-index") {
      std::fprintf(stderr,"oec_a owns the OEC zero-part index path; initialize the archive with oecinit first\n"); return 2;
    } else pass.push_back(a);
  }
  (void)idx_explicit;
  std::string verr;
  if (!zfec::validate_options(ecopt, verr)) { std::fprintf(stderr,"bad EC options: %s\n",verr.c_str()); return 2; }
  if (pass.empty()) { std::fprintf(stderr,"oec_a: missing source/add arguments\n"); return 2; }

  OecIgnorePlan ignore_plan; std::string ignore_err, ignore_exclude_file;
  if(!oec_prepare_ignore_plan(pass,use_gitignore,ignore_plan,ignore_err)) { std::fprintf(stderr,"oec_a: ignore preparation failed: %s\n",ignore_err.c_str()); return 2; }
  if(ignore_plan.active) {
    std::fprintf(stdout,"oec_a: ignore rules loaded from %llu file(s); scanned=%llu ignored_files=%llu ignored_dirs=%llu\n",
      (unsigned long long)ignore_plan.rule_files.size(),(unsigned long long)ignore_plan.scanned_files,
      (unsigned long long)ignore_plan.ignored_files,(unsigned long long)ignore_plan.ignored_dirs);
    if(!oec_write_ignore_exclude_file(ignore_plan,ignore_exclude_file,ignore_err)) { std::fprintf(stderr,"oec_a: %s\n",ignore_err.c_str()); return 2; }
  }

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
  if(!ignore_exclude_file.empty()){ child.push_back("-exclude"); child.push_back(ignore_exclude_file); }
  child.push_back("-index"); child.push_back(index);

  const int rc=spawn_self(exe, child);
  if(!ignore_exclude_file.empty()) std::remove(ignore_exclude_file.c_str());
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
  {
    std::string jerr;
    if(!oec_progressive_json_after_add(exe,base,index,pass,json_force,jerr,ignore_plan.active?&ignore_plan:0)) {
      if(!jerr.empty()) { std::fprintf(stderr,"oec_a: archive update is valid, JSON refresh failed: %s\n",jerr.c_str()); return 8; }
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


struct OecJsonFileRecord {
  std::string path;
  uint64_t size;
  std::string modified;
  std::string attributes;
  std::string type;
  uint64_t version;
  int ratio_percent;
  char status;
  std::string md5;
  std::string md5_source;
  OecJsonFileRecord(): size(0), version(0), ratio_percent(-1), status('+') {}
};

inline std::string oec_norm_relpath(std::string p) {
  for(size_t i=0;i<p.size();++i) if(p[i]=='\\') p[i]='/';
  while(p.size()>=2 && p[0]=='.' && p[1]=='/') p.erase(0,2);
  if(p.size()>=2 && ((p[0]>='A'&&p[0]<='Z')||(p[0]>='a'&&p[0]<='z')) && p[1]==':') p.erase(0,2);
  while(!p.empty() && p[0]=='/') p.erase(0,1);
  std::string out; out.reserve(p.size()); bool slash=false;
  for(size_t i=0;i<p.size();++i){ char c=p[i]; if(c=='/'){ if(!slash) out+=c; slash=true; } else { out+=c; slash=false; } }
  return out;
}

inline std::string oec_basename(std::string p) {
  while(!p.empty() && (p[p.size()-1]=='/'||p[p.size()-1]=='\\')) p.resize(p.size()-1);
  size_t q=last_path_separator(p); return q==std::string::npos?p:p.substr(q+1);
}
inline std::string oec_path_join(const std::string& a,const std::string& b){ if(a.empty())return b; if(b.empty())return a; char c=a[a.size()-1]; return (c=='/'||c=='\\')?a+b:a+"/"+b; }


inline int oec_path_kind(const std::string& p, uint64_t* size=0);

inline std::string oec_dirname(std::string p) {
  while(p.size()>1 && (p[p.size()-1]=='/'||p[p.size()-1]=='\\')) p.resize(p.size()-1);
  const size_t q=last_path_separator(p);
  if(q==std::string::npos) return ".";
  if(q==0) return p.substr(0,1);
  return p.substr(0,q);
}

inline bool oec_read_text_lines(const std::string& path,std::vector<std::string>& lines,std::string& err) {
  lines.clear(); FILE* f=std::fopen(path.c_str(),"rb"); if(!f){err="cannot open ignore file: "+path;return false;}
  std::string cur; char buf[4096];
  while(std::fgets(buf,sizeof(buf),f)) {
    cur += buf;
    if(!cur.empty() && cur[cur.size()-1]=='\n') {
      while(!cur.empty() && (cur[cur.size()-1]=='\n'||cur[cur.size()-1]=='\r')) cur.resize(cur.size()-1);
      lines.push_back(cur); cur.clear();
    }
  }
  if(std::ferror(f)){std::fclose(f);err="cannot read ignore file: "+path;return false;}
  std::fclose(f); if(!cur.empty()) lines.push_back(cur); return true;
}

inline bool oec_escaped_at(const std::string& s,size_t pos) {
  size_t n=0; while(pos>0 && s[pos-1]=='\\'){++n;--pos;} return (n&1)!=0;
}

inline std::string oec_git_unescape(const std::string& s) {
  std::string out; out.reserve(s.size());
  for(size_t i=0;i<s.size();++i) {
    if(s[i]=='\\' && i+1<s.size() && (s[i+1]=='#'||s[i+1]=='!'||s[i+1]==' '||s[i+1]=='\\')) out+=s[++i];
    else out+=s[i];
  }
  return out;
}

inline bool oec_parse_ignore_file(const std::string& path,std::vector<OecIgnoreRule>& rules,std::string& err) {
  std::vector<std::string> lines; if(!oec_read_text_lines(path,lines,err)) return false;
  for(size_t li=0;li<lines.size();++li) {
    std::string x=lines[li];
    // Git ignores unescaped trailing spaces.
    while(!x.empty() && x[x.size()-1]==' ' && !oec_escaped_at(x,x.size()-1)) x.resize(x.size()-1);
    if(x.empty()) continue;
    if(x[0]=='#') continue;
    OecIgnoreRule r;
    if(x[0]=='!' && !oec_escaped_at(x,0)) { r.negated=true; x.erase(0,1); }
    else if(x.size()>=2 && x[0]=='\\' && (x[1]=='!'||x[1]=='#')) x.erase(0,1);
    if(x.empty()) continue;
    for(size_t i=0;i<x.size();++i) if(x[i]=='\\' && i+1<x.size() && x[i+1]=='\\') { /* keep escaped slash semantics simple */ }
    if(!x.empty() && x[0]=='/'){ r.anchored=true; x.erase(0,1); }
    if(!x.empty() && x[x.size()-1]=='/' && !oec_escaped_at(x,x.size()-1)) { r.dir_only=true; x.resize(x.size()-1); }
    x=oec_git_unescape(x); for(size_t i=0;i<x.size();++i) if(x[i]=='\\') x[i]='/';
    while(x.size()>=2 && x[0]=='.' && x[1]=='/') x.erase(0,2);
    if(x.empty()) continue;
    r.pattern=x; r.has_slash=(x.find('/')!=std::string::npos); rules.push_back(r);
  }
  return true;
}

inline bool oec_glob_class_match(const std::string& pat,size_t& pi,char c) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  if(c>='A'&&c<='Z') c=(char)(c-'A'+'a');
#endif
  bool neg=false,hit=false; ++pi;
  if(pi<pat.size() && (pat[pi]=='!'||pat[pi]=='^')) {neg=true;++pi;}
  char prev=0; bool have_prev=false;
  while(pi<pat.size() && pat[pi]!=']') {
    char a=pat[pi++];
    if(a=='\\' && pi<pat.size()) a=pat[pi++];
    if(a=='-' && have_prev && pi<pat.size() && pat[pi]!=']') {
      char b=pat[pi++]; if(b=='\\' && pi<pat.size()) b=pat[pi++];
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
      if(b>='A'&&b<='Z')b=(char)(b-'A'+'a');
#endif
      if(c>=prev && c<=b) hit=true; have_prev=false; continue;
    }
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    if(a>='A'&&a<='Z')a=(char)(a-'A'+'a');
#endif
    if(c==a) hit=true; prev=a; have_prev=true;
  }
  if(pi<pat.size() && pat[pi]==']') ++pi;
  return neg?!hit:hit;
}

inline bool oec_git_glob_rec(const std::string& pat,size_t pi,const std::string& text,size_t ti) {
  while(pi<pat.size()) {
    if(pat[pi]=='*') {
      if(pi+1<pat.size() && pat[pi+1]=='*') {
        while(pi+1<pat.size() && pat[pi+1]=='*') ++pi;
        ++pi;
        if(pi<pat.size() && pat[pi]=='/') {
          ++pi;
          if(oec_git_glob_rec(pat,pi,text,ti)) return true;
          for(size_t k=ti;k<text.size();++k) if(text[k]=='/' && oec_git_glob_rec(pat,pi,text,k+1)) return true;
          return false;
        }
        for(size_t k=ti;k<=text.size();++k) if(oec_git_glob_rec(pat,pi,text,k)) return true;
        return false;
      }
      ++pi;
      for(size_t k=ti;;++k) {
        if(oec_git_glob_rec(pat,pi,text,k)) return true;
        if(k>=text.size() || text[k]=='/') break;
      }
      return false;
    }
    if(ti>=text.size()) return false;
    if(pat[pi]=='?') { if(text[ti]=='/') return false; ++pi;++ti; continue; }
    if(pat[pi]=='[') {
      if(text[ti]=='/') return false; size_t q=pi; if(!oec_glob_class_match(pat,q,text[ti])) return false; pi=q;++ti;continue;
    }
    char pc=pat[pi++]; if(pc=='\\' && pi<pat.size()) pc=pat[pi++];
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    char tc=text[ti++]; if(pc>='A'&&pc<='Z')pc=(char)(pc-'A'+'a'); if(tc>='A'&&tc<='Z')tc=(char)(tc-'A'+'a'); if(pc!=tc)return false;
#else
    if(pc!=text[ti++]) return false;
#endif
  }
  return ti==text.size();
}

inline bool oec_git_glob_match(const std::string& pat,const std::string& text) { return oec_git_glob_rec(pat,0,text,0); }

inline bool oec_ignore_rule_matches(const OecIgnoreRule& r,const std::string& rel,bool is_dir) {
  if(r.dir_only && !is_dir) return false;
  if(r.has_slash || r.anchored) return oec_git_glob_match(r.pattern,rel);
  size_t start=0;
  for(;;) {
    size_t slash=rel.find('/',start); std::string comp=rel.substr(start,slash==std::string::npos?std::string::npos:slash-start);
    if(oec_git_glob_match(r.pattern,comp)) return true;
    if(slash==std::string::npos) break; start=slash+1;
  }
  return false;
}

inline bool oec_ignore_eval_one(const std::vector<OecIgnoreRule>& rules,const std::string& rel,bool is_dir) {
  bool ignored=false;
  for(size_t i=0;i<rules.size();++i) if(oec_ignore_rule_matches(rules[i],rel,is_dir)) ignored=!rules[i].negated;
  return ignored;
}

inline bool oec_ignore_path(const std::vector<OecIgnoreRule>& rules,const std::string& rel,bool is_dir) {
  std::string p=oec_norm_relpath(rel);
  size_t pos=0;
  while(true) {
    size_t slash=p.find('/',pos); if(slash==std::string::npos) break;
    const std::string dir=p.substr(0,slash);
    if(oec_ignore_eval_one(rules,dir,true)) return true;
    pos=slash+1;
  }
  return oec_ignore_eval_one(rules,p,is_dir);
}

inline std::vector<std::string> oec_add_source_args(const std::vector<std::string>& args) {
  std::vector<std::string> roots;
  const char* value_opts[]={"-key","-franzen","-method","-threads","-to","-not","-only","-since","-until","-version","-index","-exclude","-include","-minsize","-maxsize"};
  for(size_t i=0;i<args.size();++i) {
    bool value=false; if(i) for(size_t j=0;j<sizeof(value_opts)/sizeof(value_opts[0]);++j) if(args[i-1]==value_opts[j]){value=true;break;}
    if(value||args[i].empty()||args[i][0]=='-') continue;
    if(oec_path_kind(args[i])!=0) roots.push_back(args[i]);
  }
  return roots;
}

inline std::string oec_native_source_path(const std::string& root,const std::string& rel) {
  std::string p=rel.empty()?root:oec_path_join(root,rel); for(size_t i=0;i<p.size();++i)if(p[i]=='\\')p[i]='/'; return p;
}

inline bool oec_scan_ignore_root(const std::string& root,const std::string& rel,const std::vector<OecIgnoreRule>& rules,OecIgnorePlan& plan,std::string& err) {
  const std::string here=rel.empty()?root:oec_path_join(root,rel);
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  std::string pat=oec_path_join(here,"*"); struct _finddata_t fd; intptr_t h=_findfirst(pat.c_str(),&fd);
  if(h==-1){err="cannot enumerate source directory for ignore filtering: "+here;return false;}
  do { std::string n=fd.name;if(n=="."||n=="..")continue;std::string r=rel.empty()?n:oec_path_join(rel,n),full=oec_path_join(root,r);bool dir=(fd.attrib&_A_SUBDIR)!=0;
    if(dir){ if(oec_ignore_path(rules,oec_norm_relpath(r),true)){plan.native_excludes.push_back(oec_native_source_path(root,r));++plan.ignored_dirs;} else if(!oec_scan_ignore_root(root,r,rules,plan,err)){_findclose(h);return false;} }
    else {++plan.scanned_files;if(oec_ignore_path(rules,oec_norm_relpath(r),false)){plan.native_excludes.push_back(oec_native_source_path(root,r));++plan.ignored_files;} else {OecIgnoreFile f;f.source_root=root;f.rel=oec_norm_relpath(r);f.full=full;plan.allowed_files.push_back(f);}}
  }while(_findnext(h,&fd)==0);_findclose(h);return true;
#else
  DIR* d=opendir(here.c_str());if(!d){err="cannot enumerate source directory for ignore filtering: "+here;return false;}struct dirent* de;
  while((de=readdir(d))!=0){std::string n=de->d_name;if(n=="."||n=="..")continue;std::string r=rel.empty()?n:oec_path_join(rel,n),full=oec_path_join(root,r);int kind=oec_path_kind(full);
    if(kind==2){if(oec_ignore_path(rules,oec_norm_relpath(r),true)){plan.native_excludes.push_back(oec_native_source_path(root,r));++plan.ignored_dirs;}else if(!oec_scan_ignore_root(root,r,rules,plan,err)){closedir(d);return false;}}
    else if(kind==1){++plan.scanned_files;if(oec_ignore_path(rules,oec_norm_relpath(r),false)){plan.native_excludes.push_back(oec_native_source_path(root,r));++plan.ignored_files;}else{OecIgnoreFile f;f.source_root=root;f.rel=oec_norm_relpath(r);f.full=full;plan.allowed_files.push_back(f);}}
  }closedir(d);return true;
#endif
}

inline bool oec_prepare_ignore_plan(const std::vector<std::string>& add_args,bool use_gitignore,OecIgnorePlan& plan,std::string& err) {
  plan=OecIgnorePlan();plan.gitignore_enabled=use_gitignore;
  const std::vector<std::string> roots=oec_add_source_args(add_args);
  struct RootRules{std::string root;int kind;std::vector<OecIgnoreRule> rules;}; std::vector<RootRules> rr;
  bool any=false;
  for(size_t i=0;i<roots.size();++i){RootRules x;x.root=roots[i];x.kind=oec_path_kind(x.root);std::string dir=x.kind==2?x.root:oec_dirname(x.root);
    if(use_gitignore){std::string g=oec_path_join(dir,".gitignore");if(path_exists(g)){if(!oec_parse_ignore_file(g,x.rules,err))return false;plan.rule_files.push_back(g);any=true;}}
    std::string z=oec_path_join(dir,"zpaq.ignore");if(path_exists(z)){if(!oec_parse_ignore_file(z,x.rules,err))return false;plan.rule_files.push_back(z);any=true;}
    rr.push_back(x);
  }
  if(!any) return true;
  plan.active=true;
  for(size_t i=0;i<rr.size();++i){
    if(rr[i].kind==2){if(!oec_scan_ignore_root(rr[i].root,"",rr[i].rules,plan,err))return false;}
    else if(rr[i].kind==1){std::string name=oec_basename(rr[i].root);++plan.scanned_files;if(oec_ignore_path(rr[i].rules,name,false)){plan.native_excludes.push_back(oec_native_source_path(rr[i].root,""));++plan.ignored_files;}else{OecIgnoreFile f;f.source_root=rr[i].root;f.rel=name;f.full=rr[i].root;plan.allowed_files.push_back(f);}}
  }
  std::sort(plan.native_excludes.begin(),plan.native_excludes.end());plan.native_excludes.erase(std::unique(plan.native_excludes.begin(),plan.native_excludes.end()),plan.native_excludes.end());
  return true;
}

inline std::string oec_ignore_temp_path() {
  const char* d=std::getenv("TMPDIR");
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  if(!d||!*d)d=std::getenv("TEMP");if(!d||!*d)d=std::getenv("TMP");
#endif
  std::string dir=(d&&*d)?d:".";
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  unsigned long pid=(unsigned long)_getpid();
#else
  unsigned long pid=(unsigned long)getpid();
#endif
  std::ostringstream o;o<<"zpaqoec-ignore-"<<pid<<"-"<<(unsigned long long)std::time(0)<<".txt";return oec_path_join(dir,o.str());
}

inline bool oec_write_ignore_exclude_file(const OecIgnorePlan& plan,std::string& path,std::string& err) {
  path.clear();if(!plan.active||plan.native_excludes.empty())return true;path=oec_ignore_temp_path();FILE* f=std::fopen(path.c_str(),"wb");if(!f){err="cannot create temporary native exclude list: "+path;return false;}
  for(size_t i=0;i<plan.native_excludes.size();++i)if(std::fprintf(f,"%s\n",plan.native_excludes[i].c_str())<0){std::fclose(f);std::remove(path.c_str());err="cannot write temporary native exclude list: "+path;return false;}
  if(std::fclose(f)!=0){std::remove(path.c_str());err="cannot close temporary native exclude list: "+path;return false;}return true;
}

inline int oec_path_kind(const std::string& p, uint64_t* size) {
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  struct _stat64 st; if(_stat64(p.c_str(),&st)!=0) return 0; if(size)*size=(uint64_t)st.st_size; return (st.st_mode&_S_IFDIR)?2:1;
#else
  struct stat st; if(stat(p.c_str(),&st)!=0) return 0; if(size)*size=(uint64_t)st.st_size; return S_ISDIR(st.st_mode)?2:(S_ISREG(st.st_mode)?1:0);
#endif
}

inline bool oec_walk_files(const std::string& root,const std::string& rel,std::vector<std::pair<std::string,std::string> >& out,std::string& err){
  const std::string here=rel.empty()?root:oec_path_join(root,rel);
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  std::string pat=oec_path_join(here,"*"); struct _finddata_t fd; intptr_t h=_findfirst(pat.c_str(),&fd);
  if(h==-1){err="cannot enumerate source directory: "+here;return false;}
  do { std::string n=fd.name; if(n=="."||n=="..")continue; std::string r=rel.empty()?n:oec_path_join(rel,n); std::string full=oec_path_join(root,r);
       if(fd.attrib&_A_SUBDIR){ if(!oec_walk_files(root,r,out,err)){_findclose(h);return false;} } else out.push_back(std::make_pair(oec_norm_relpath(r),full));
  } while(_findnext(h,&fd)==0); _findclose(h); return true;
#else
  DIR* d=opendir(here.c_str()); if(!d){err="cannot enumerate source directory: "+here;return false;} struct dirent* de;
  while((de=readdir(d))!=0){ std::string n=de->d_name; if(n=="."||n=="..")continue; std::string r=rel.empty()?n:oec_path_join(rel,n), full=oec_path_join(root,r); int k=oec_path_kind(full);
    if(k==2){ if(!oec_walk_files(root,r,out,err)){closedir(d);return false;} } else if(k==1) out.push_back(std::make_pair(oec_norm_relpath(r),full));
  } closedir(d); return true;
#endif
}

inline void oec_add_md5_key(std::map<std::string,std::string>& m,const std::string& key,const std::string& md5){
  std::string k=oec_norm_relpath(key); if(k.empty())return; std::map<std::string,std::string>::iterator it=m.find(k);
  if(it==m.end())m[k]=md5; else if(it->second!=md5)it->second.clear();
}

inline bool oec_collect_source_md5(const std::vector<std::string>& args,std::map<std::string,std::string>& hashes,std::string& err,const OecIgnorePlan* ignore_plan=0){
  hashes.clear();
  if(ignore_plan && ignore_plan->active){uint64_t count=0;for(size_t j=0;j<ignore_plan->allowed_files.size();++j){const OecIgnoreFile& f=ignore_plan->allowed_files[j];std::string md5;if(!oecmd5::file(f.full,md5,err))return false;++count;const std::string rb=oec_basename(f.source_root);oec_add_md5_key(hashes,f.rel,md5);oec_add_md5_key(hashes,oec_path_join(rb,f.rel),md5);oec_add_md5_key(hashes,f.full,md5);if((count%250)==0){std::fprintf(stdout,"oec json: source MD5 progress %llu files\n",(unsigned long long)count);std::fflush(stdout);}}std::fprintf(stdout,"oec json: source MD5 collected for %llu non-ignored files\n",(unsigned long long)count);return true;}
  std::set<std::string> roots;
  const char* value_opts[]={"-key","-franzen","-method","-threads","-to","-not","-only","-since","-until","-version","-index"};
  for(size_t i=0;i<args.size();++i){ bool skip=false; if(i){ for(size_t j=0;j<sizeof(value_opts)/sizeof(value_opts[0]);++j) if(args[i-1]==value_opts[j]){skip=true;break;} } if(skip||args[i].empty()||args[i][0]=='-')continue; if(oec_path_kind(args[i])!=0)roots.insert(args[i]); }
  uint64_t count=0;
  for(std::set<std::string>::const_iterator ri=roots.begin();ri!=roots.end();++ri){ const std::string root=*ri; int kind=oec_path_kind(root); std::vector<std::pair<std::string,std::string> > fs;
    if(kind==1) fs.push_back(std::make_pair(oec_basename(root),root)); else if(kind==2 && !oec_walk_files(root,"",fs,err)) return false;
    const std::string rb=oec_basename(root);
    for(size_t j=0;j<fs.size();++j){ std::string md5; if(!oecmd5::file(fs[j].second,md5,err))return false; ++count;
      oec_add_md5_key(hashes,fs[j].first,md5); oec_add_md5_key(hashes,oec_path_join(rb,fs[j].first),md5); oec_add_md5_key(hashes,fs[j].second,md5);
      if((count%250)==0){std::fprintf(stdout,"oec json: source MD5 progress %llu files\n",(unsigned long long)count);std::fflush(stdout);}
    }
  }
  if(!roots.empty()) std::fprintf(stdout,"oec json: source MD5 collected for %llu files\n",(unsigned long long)count);
  return true;
}

inline std::string oec_trim_copy(const std::string& in) {
  size_t a=0, b=in.size();
  while(a<b && (in[a]==' ' || in[a]=='\t' || in[a]=='\r' || in[a]=='\n')) ++a;
  while(b>a && (in[b-1]==' ' || in[b-1]=='\t' || in[b-1]=='\r' || in[b-1]=='\n')) --b;
  return in.substr(a,b-a);
}

inline std::vector<std::string> oec_split_ws(const std::string& in) {
  std::vector<std::string> out;
  size_t i=0;
  while(i<in.size()) {
    while(i<in.size() && (in[i]==' ' || in[i]=='\t')) ++i;
    if(i>=in.size()) break;
    size_t j=i;
    while(j<in.size() && in[j]!=' ' && in[j]!='\t') ++j;
    out.push_back(in.substr(i,j-i)); i=j;
  }
  return out;
}

inline bool oec_parse_grouped_u64(const std::string& token, uint64_t& out) {
  out=0; bool any=false;
  for(size_t i=0;i<token.size();++i) {
    const char c=token[i];
    if(c>='0' && c<='9') {
      any=true;
      const uint64_t d=static_cast<uint64_t>(c-'0');
      if(out > (UINT64_MAX-d)/10u) return false;
      out=out*10u+d;
    } else if(c=='.' || c==',' || c=='\'' || c=='_') {
      continue;
    } else return false;
  }
  return any;
}

inline bool oec_parse_u64_digits(const std::string& token, uint64_t& out) {
  out=0; if(token.empty()) return false;
  for(size_t i=0;i<token.size();++i) {
    const char c=token[i]; if(c<'0'||c>'9') return false;
    const uint64_t d=static_cast<uint64_t>(c-'0');
    if(out > (UINT64_MAX-d)/10u) return false;
    out=out*10u+d;
  }
  return true;
}

inline bool oec_looks_date(const std::string& s) {
  if(s.size()==10 && (s[4]=='-'||s[4]=='/') && s[7]==s[4]) {
    for(size_t i=0;i<s.size();++i) if(i!=4&&i!=7 && (s[i]<'0'||s[i]>'9')) return false;
    return true;
  }
  if(s.size()==8) { for(size_t i=0;i<8;++i) if(s[i]<'0'||s[i]>'9') return false; return true; }
  return false;
}
inline bool oec_looks_time(const std::string& s) {
  if(s.size()==8 && s[2]==':' && s[5]==':') {
    for(size_t i=0;i<8;++i) if(i!=2&&i!=5 && (s[i]<'0'||s[i]>'9')) return false;
    return true;
  }
  if(s.size()==6) { for(size_t i=0;i<6;++i) if(s[i]<'0'||s[i]>'9') return false; return true; }
  return false;
}
inline std::string oec_isoish_datetime(std::string d,std::string t) {
  if(d.size()==8) d=d.substr(0,4)+"-"+d.substr(4,2)+"-"+d.substr(6,2);
  if(d.size()==10 && d[4]=='/') { d[4]='-'; d[7]='-'; }
  if(t.size()==6) t=t.substr(0,2)+":"+t.substr(2,2)+":"+t.substr(4,2);
  return d + (t.empty()?std::string():std::string("T")+t);
}
inline std::string oec_strip_ansi_copy(const std::string& in) {
  std::string out; out.reserve(in.size());
  for(size_t i=0;i<in.size();) {
    if((unsigned char)in[i]==0x1b && i+1<in.size() && in[i+1]=='[') {
      i+=2; while(i<in.size()) { char c=in[i++]; if((c>='@'&&c<='~')) break; }
    } else out+=in[i++];
  }
  return out;
}
inline bool oec_looks_attr_token(const std::string& s) {
  if(s.empty()) return false;
  if(s.size()==4 && s[0]=='0') { for(size_t i=1;i<4;++i) if(s[i]<'0'||s[i]>'7') return false; return true; }
  if((s[0]=='d'||s[0]=='-'||s[0]=='l') && s.size()>=4) return true;
  bool alpha=false; for(size_t i=0;i<s.size();++i) { char c=s[i]; if((c>='A'&&c<='Z')||(c>='a'&&c<='z')) alpha=true; else if(c!='-'&&c!='.') return false; }
  return alpha && s.size()<=16;
}
struct OecTokSpan { std::string s; size_t a,b; OecTokSpan(const std::string& x,size_t aa,size_t bb):s(x),a(aa),b(bb){} };
inline std::vector<OecTokSpan> oec_split_ws_spans(const std::string& in) {
  std::vector<OecTokSpan> out; size_t i=0;
  while(i<in.size()) { while(i<in.size()&&(in[i]==' '||in[i]=='\t'))++i; if(i>=in.size())break; size_t a=i; while(i<in.size()&&in[i]!=' '&&in[i]!='\t')++i; out.push_back(OecTokSpan(in.substr(a,i-a),a,i)); }
  return out;
}
inline void oec_finish_record_type(OecJsonFileRecord& r) {
  bool isdir=false; if(!r.attributes.empty()&&(r.attributes[0]=='d'||(r.attributes.size()>4&&r.attributes[4]=='D')))isdir=true;
  if(!r.path.empty() && (r.path[r.path.size()-1]=='/'||r.path[r.path.size()-1]=='\\')) isdir=true;
  r.type=isdir?"directory":"file";
}
inline bool oec_parse_metadata_left(const std::string& left, OecJsonFileRecord& r) {
  const std::vector<std::string> t=oec_split_ws(left); if(t.size()<2) return false;
  size_t di=t.size(), ti=t.size();
  for(size_t i=0;i<t.size();++i) if(oec_looks_date(t[i])) { di=i; if(i+1<t.size()&&oec_looks_time(t[i+1]))ti=i+1; break; }
  if(di==t.size()) return false;
  r.modified=oec_isoish_datetime(t[di],ti<t.size()?t[ti]:std::string()); r.attributes.clear(); r.size=0; r.ratio_percent=-1; r.version=0;
  size_t begin=(ti<t.size()?ti+1:di+1), attri=t.size();
  for(size_t i=begin;i<t.size();++i) if(oec_looks_attr_token(t[i])) { attri=i; r.attributes=t[i]; break; }
  // Version is normally the final integer before |status.
  for(size_t i=t.size();i>begin;--i) { uint64_t v=0; if(oec_parse_u64_digits(t[i-1],v) && i-1!=attri) { r.version=v; break; } }
  for(size_t i=begin;i<t.size();++i) if(!t[i].empty()&&t[i][t[i].size()-1]=='%') { uint64_t v=0; if(oec_parse_u64_digits(t[i].substr(0,t[i].size()-1),v)&&v<=10000)r.ratio_percent=(int)v; }
  // Prefer numeric token adjacent to attributes; then first numeric token that is not ratio/version/attrs.
  bool have=false; uint64_t sz=0;
  if(attri<t.size()) {
    if(attri+1<t.size() && oec_parse_grouped_u64(t[attri+1],sz) && t[attri+1].find('%')==std::string::npos) have=true;
    else if(attri>begin && oec_parse_grouped_u64(t[attri-1],sz)) have=true;
  }
  if(!have) for(size_t i=begin;i<t.size();++i) {
    if(i==attri || (!t[i].empty()&&t[i][t[i].size()-1]=='%')) continue;
    uint64_t v=0; if(!oec_parse_grouped_u64(t[i],v))continue;
    if(r.version && v==r.version && i+1==t.size())continue; sz=v; have=true; break;
  }
  if(!have) return false; r.size=sz; return true;
}
inline bool oec_parse_list_record_pipe(const std::string& rawline, OecJsonFileRecord& r) {
  const std::string line=oec_strip_ansi_copy(rawline); const size_t bar=line.find('|'); if(bar==std::string::npos)return false;
  const std::string left=oec_trim_copy(line.substr(0,bar)); std::string right=oec_trim_copy(line.substr(bar+1)); if(right.empty())return false;
  const char status=right[0]; if(status!='+'&&status!='-'&&status!='#'&&status!='=')return false; right=oec_trim_copy(right.substr(1)); if(right.empty()||right=="?")return false;
  r.path=right; r.status=status; r.ratio_percent=-1; r.version=0; r.size=0; r.attributes.clear(); r.modified.clear();
  if(left.find("deleted/inacessible")==0 || left.find("deleted/inaccessible")==0) {
    const std::vector<std::string> t=oec_split_ws(left); for(size_t i=t.size();i>0;--i){uint64_t v=0;if(oec_parse_u64_digits(t[i-1],v)){r.version=v;break;}}
    r.type="file"; return true;
  }
  if(!oec_parse_metadata_left(left,r)) return false; oec_finish_record_type(r); return true;
}
inline bool oec_parse_list_record_plain(const std::string& rawline, OecJsonFileRecord& r) {
  std::string s=oec_trim_copy(oec_strip_ansi_copy(rawline)); if(s.empty())return false; char status='+';
  if((s[0]=='-'||s[0]=='+'||s[0]=='='||s[0]=='#') && s.size()>1 && (s[1]==' '||s[1]=='\t')) { status=s[0]; s=oec_trim_copy(s.substr(1)); }
  const std::vector<OecTokSpan> ts=oec_split_ws_spans(s); if(ts.size()<4)return false;
  size_t di=ts.size(),ti=ts.size(); for(size_t i=0;i<ts.size();++i)if(oec_looks_date(ts[i].s)){di=i;if(i+1<ts.size()&&oec_looks_time(ts[i+1].s))ti=i+1;break;} if(di==ts.size())return false;
  size_t begin=ti<ts.size()?ti+1:di+1, attri=ts.size(), sizei=ts.size(), ratioi=ts.size(), veri=ts.size();
  for(size_t i=begin;i<ts.size();++i) if(oec_looks_attr_token(ts[i].s)){attri=i;break;}
  if(attri<ts.size()) { uint64_t z=0; if(attri+1<ts.size()&&oec_parse_grouped_u64(ts[attri+1].s,z))sizei=attri+1; else if(attri>begin&&oec_parse_grouped_u64(ts[attri-1].s,z))sizei=attri-1; }
  if(sizei==ts.size()) for(size_t i=begin;i<ts.size();++i){uint64_t z=0;if(i!=attri&&oec_parse_grouped_u64(ts[i].s,z)){sizei=i;break;}}
  for(size_t i=begin;i<ts.size();++i) if(!ts[i].s.empty()&&ts[i].s[ts[i].s.size()-1]=='%') ratioi=i;
  for(size_t i=ts.size();i>begin;--i){uint64_t z=0;if(i-1!=attri&&oec_parse_u64_digits(ts[i-1].s,z)){veri=i-1;break;}}
  size_t last=begin; if(attri<ts.size())last=std::max(last,attri);if(sizei<ts.size())last=std::max(last,sizei);if(ratioi<ts.size())last=std::max(last,ratioi);if(veri<ts.size()&&veri>last)last=veri;
  if(last+1>=ts.size())return false; r.path=oec_trim_copy(s.substr(ts[last+1].a)); if(r.path.empty())return false; r.status=status;
  std::string left=s.substr(0,ts[last].b); if(!oec_parse_metadata_left(left,r))return false; oec_finish_record_type(r); return true;
}
inline void oec_parse_file_list(const std::string& text, std::vector<OecJsonFileRecord>& files) {
  files.clear(); size_t pos=0; while(pos<text.size()) { size_t end=text.find('\n',pos);if(end==std::string::npos)end=text.size();std::string line=text.substr(pos,end-pos);if(!line.empty()&&line[line.size()-1]=='\r')line.resize(line.size()-1);OecJsonFileRecord r;if(oec_parse_list_record_pipe(line,r)||oec_parse_list_record_plain(line,r))files.push_back(r);pos=end<text.size()?end+1:end; }
}
inline void oec_collapse_current_records(std::vector<OecJsonFileRecord>& files) {
  std::map<std::string,OecJsonFileRecord> cur; for(size_t i=0;i<files.size();++i){std::string k=oec_norm_relpath(files[i].path);if(k.empty())continue;std::map<std::string,OecJsonFileRecord>::iterator it=cur.find(k);if(it==cur.end()||files[i].version>=it->second.version)cur[k]=files[i];}
  files.clear(); for(std::map<std::string,OecJsonFileRecord>::const_iterator it=cur.begin();it!=cur.end();++it)if(it->second.status!='-')files.push_back(it->second);
}
inline std::string oec_json_escape(const std::string& s) {
  static const char* hex="0123456789abcdef";
  std::string out; out.reserve(s.size()+16);
  for(size_t i=0;i<s.size();++i) {
    const unsigned char c=static_cast<unsigned char>(s[i]);
    switch(c) {
      case '"': out+="\\\""; break;
      case '\\': out+="\\\\"; break;
      case '\b': out+="\\b"; break;
      case '\f': out+="\\f"; break;
      case '\n': out+="\\n"; break;
      case '\r': out+="\\r"; break;
      case '\t': out+="\\t"; break;
      default:
        if(c<0x20) { out+="\\u00"; out+=hex[(c>>4)&15]; out+=hex[c&15]; }
        else out+=static_cast<char>(c);
    }
  }
  return out;
}

inline std::string oec_json_output_path(const std::string& archive_spec) {
  const size_t sep=last_path_separator(archive_spec);
  const std::string dir=sep==std::string::npos?std::string():archive_spec.substr(0,sep+1);
  return dir + oec_password_stem_from_archive(archive_spec) + ".json";
}

inline bool oec_json_unescape(const std::string& s,std::string& out){
  out.clear(); for(size_t i=0;i<s.size();++i){ char c=s[i]; if(c!='\\'){out+=c;continue;} if(++i>=s.size())return false; c=s[i];
    if(c=='"'||c=='\\'||c=='/')out+=c; else if(c=='b')out+='\b'; else if(c=='f')out+='\f'; else if(c=='n')out+='\n'; else if(c=='r')out+='\r'; else if(c=='t')out+='\t';
    else if(c=='u'){ if(i+4>=s.size())return false; unsigned v=0; for(int k=0;k<4;++k){char h=s[++i];v<<=4;if(h>='0'&&h<='9')v+=h-'0';else if(h>='a'&&h<='f')v+=h-'a'+10;else if(h>='A'&&h<='F')v+=h-'A'+10;else return false;}
      if(v<0x80)out+=(char)v; else if(v<0x800){out+=(char)(0xC0|(v>>6));out+=(char)(0x80|(v&63));} else {out+=(char)(0xE0|(v>>12));out+=(char)(0x80|((v>>6)&63));out+=(char)(0x80|(v&63));}}
    else return false;
  } return true;
}

inline bool oec_json_line_string_field(const std::string& line,const char* name,std::string& out){
  std::string needle=std::string("\"")+name+"\":\""; size_t p=line.find(needle); if(p==std::string::npos)return false; p+=needle.size(); std::string raw;
  bool esc=false; for(size_t i=p;i<line.size();++i){char c=line[i]; if(!esc&&c=='"')return oec_json_unescape(raw,out); raw+=c; if(esc)esc=false; else if(c=='\\')esc=true;} return false;
}
inline bool oec_json_line_u64_field(const std::string& line,const char* name,uint64_t& out){ std::string n=std::string("\"")+name+"\":"; size_t p=line.find(n); if(p==std::string::npos)return false;p+=n.size();size_t e=p;while(e<line.size()&&line[e]>='0'&&line[e]<='9')++e;return oec_parse_u64_digits(line.substr(p,e-p),out); }

inline bool oec_load_existing_json_records(const std::string& path,std::map<std::string,OecJsonFileRecord>& out,std::string& err){
  out.clear(); FILE* f=std::fopen(path.c_str(),"rb"); if(!f){err="cannot open existing JSON: "+path;return false;} char buf[262144];
  while(std::fgets(buf,sizeof(buf),f)){ std::string line=buf; if(line.find("{\"path\":")==std::string::npos)continue; OecJsonFileRecord r; if(!oec_json_line_string_field(line,"path",r.path))continue;
    oec_json_line_u64_field(line,"size",r.size); oec_json_line_string_field(line,"modified",r.modified); oec_json_line_string_field(line,"md5",r.md5); oec_json_line_string_field(line,"md5_source",r.md5_source); out[oec_norm_relpath(r.path)]=r;
  } std::fclose(f); return true;
}

inline bool oec_install_json_tmp(const std::string& tmp,const std::string& dst,bool overwrite,std::string& err){
  if(!overwrite && path_exists(dst)){std::remove(tmp.c_str());err="output already exists, refusing to overwrite: "+dst;return false;}
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  if(overwrite){ if(!MoveFileExA(tmp.c_str(),dst.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::remove(tmp.c_str());err="cannot atomically replace JSON: "+dst;return false;} return true; }
#endif
  if(overwrite) std::remove(dst.c_str());
  if(std::rename(tmp.c_str(),dst.c_str())!=0){std::remove(tmp.c_str());err="cannot install JSON: "+dst;return false;} return true;
}

inline bool oec_write_json_catalog(const std::string& output_path, const std::string& spec,
                                   const std::string& zero_part, const std::string& source_kind,
                                   const std::vector<OecJsonFileRecord>& files, std::string& err,
                                   bool overwrite=false) {
  if(!overwrite && path_exists(output_path)) { err="output already exists, refusing to overwrite: "+output_path; return false; }
  const std::string tmp=output_path+".oecjson.tmp"; std::remove(tmp.c_str()); FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f) { err="cannot create JSON output: "+tmp; return false; }
  uint64_t total=0, hashed=0; for(size_t i=0;i<files.size();++i){total+=files[i].size;if(files[i].type=="file"&&!files[i].md5.empty())++hashed;}
  bool complete=true; for(size_t i=0;i<files.size();++i)if(files[i].type=="file"&&files[i].md5.empty()){complete=false;break;}
  bool ok=true;
  ok = ok && std::fprintf(f,"{\n  \"format\": \"zpaqoec-file-list\",\n  \"format_version\": 2,\n")>=0;
  ok = ok && std::fprintf(f,"  \"archive\": \"%s\",\n",oec_json_escape(spec).c_str())>=0;
  ok = ok && std::fprintf(f,"  \"zero_part\": \"%s\",\n",oec_json_escape(zero_part).c_str())>=0;
  ok = ok && std::fprintf(f,"  \"metadata_source\": \"%s\",\n",oec_json_escape(source_kind).c_str())>=0;
  ok = ok && std::fprintf(f,"  \"oec_version\": \"%s\",\n",kOecOverlayVersion)>=0;
  ok = ok && std::fprintf(f,"  \"generated_unix\": %llu,\n",(unsigned long long)std::time(0))>=0;
  ok = ok && std::fprintf(f,"  \"file_count\": %llu,\n  \"total_size\": %llu,\n  \"md5_file_count\": %llu,\n  \"md5_complete\": %s,\n",
                          (unsigned long long)files.size(),(unsigned long long)total,(unsigned long long)hashed,complete?"true":"false")>=0;
  ok = ok && std::fprintf(f,"  \"hash_info\": {\"whole_file_hash_algorithm\": \"MD5\", \"zpaq_fragment_integrity\": \"SHA-1\", \"note\": \"MD5 is calculated by OEC from source files during oec_a progressive updates or from extracted archive payload with oec_json --force-md5.\"},\n")>=0;
  ok = ok && std::fprintf(f,"  \"files\": [\n")>=0;
  for(size_t i=0;i<files.size() && ok;++i) {
    const OecJsonFileRecord& r=files[i];
    ok = std::fprintf(f,"    {\"path\":\"%s\",\"size\":%llu,\"modified\":\"%s\",\"attributes\":\"%s\",\"type\":\"%s\",\"version\":%llu,\"status\":\"%c\",\"compression_ratio_percent\":",
      oec_json_escape(r.path).c_str(),(unsigned long long)r.size,oec_json_escape(r.modified).c_str(),oec_json_escape(r.attributes).c_str(),r.type.c_str(),(unsigned long long)r.version,r.status)>=0;
    if(ok){if(r.ratio_percent>=0)ok=std::fprintf(f,"%d",r.ratio_percent)>=0;else ok=std::fprintf(f,"null")>=0;}
    if(ok){ if(r.md5.empty()) ok=std::fprintf(f,",\"md5\":null,\"md5_source\":null,\"hash\":null}%s\n",i+1<files.size()?",":"")>=0;
      else ok=std::fprintf(f,",\"md5\":\"%s\",\"md5_source\":\"%s\",\"hash\":{\"algorithm\":\"MD5\",\"value\":\"%s\"}}%s\n",r.md5.c_str(),oec_json_escape(r.md5_source).c_str(),r.md5.c_str(),i+1<files.size()?",":"")>=0; }
  }
  if(ok)ok=std::fprintf(f,"  ]\n}\n")>=0; if(std::fclose(f)!=0)ok=false;
  if(!ok){std::remove(tmp.c_str());err="failed while writing JSON output: "+output_path;return false;} return oec_install_json_tmp(tmp,output_path,overwrite,err);
}

inline bool oec_remove_tree(const std::string& root){
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  std::string pat=oec_path_join(root,"*"); struct _finddata_t fd; intptr_t h=_findfirst(pat.c_str(),&fd); if(h!=-1){do{std::string n=fd.name;if(n=="."||n=="..")continue;std::string p=oec_path_join(root,n);if(fd.attrib&_A_SUBDIR)oec_remove_tree(p);else std::remove(p.c_str());}while(_findnext(h,&fd)==0);_findclose(h);} return _rmdir(root.c_str())==0;
#else
  DIR* d=opendir(root.c_str()); if(d){struct dirent* de;while((de=readdir(d))!=0){std::string n=de->d_name;if(n=="."||n=="..")continue;std::string p=oec_path_join(root,n);if(oec_path_kind(p)==2)oec_remove_tree(p);else std::remove(p.c_str());}closedir(d);} return rmdir(root.c_str())==0;
#endif
}
inline bool oec_make_dir(const std::string& p){
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  return _mkdir(p.c_str())==0;
#else
  return mkdir(p.c_str(),0700)==0;
#endif
}

inline bool oec_force_md5_from_archive(const std::string& exe,const OecReadLayout& layout,const std::vector<std::string>& auth,
                                       std::vector<OecJsonFileRecord>& files,const std::string& output,std::string& err){
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
  const unsigned long pid=(unsigned long)_getpid();
#else
  const unsigned long pid=(unsigned long)getpid();
#endif
  std::ostringstream ts; ts<<output<<".md5tmp."<<pid; const std::string temp=ts.str(); if(oec_path_kind(temp)!=0)oec_remove_tree(temp); if(!oec_make_dir(temp)){err="cannot create temporary extraction directory: "+temp;return false;}
  std::fprintf(stdout,"oec_json: --force-md5 extracting archive once to %s ...\n",temp.c_str());std::fflush(stdout);
  std::vector<std::string> a; a.push_back("x");a.push_back(layout.pattern);a.push_back("-to");a.push_back(temp);a.insert(a.end(),auth.begin(),auth.end()); int rc=spawn_self(exe,a);
  if(rc!=0){oec_remove_tree(temp);std::ostringstream e;e<<"archive extraction for MD5 failed rc="<<rc;err=e.str();return false;}
  std::vector<std::pair<std::string,std::string> > disk; if(!oec_walk_files(temp,"",disk,err)){oec_remove_tree(temp);return false;}
  std::map<std::string,std::string> hashes; uint64_t n=0;
  for(size_t i=0;i<disk.size();++i){std::string md5;if(!oecmd5::file(disk[i].second,md5,err)){oec_remove_tree(temp);return false;}oec_add_md5_key(hashes,disk[i].first,md5);oec_add_md5_key(hashes,oec_basename(disk[i].first),md5);++n;if((n%250)==0){std::fprintf(stdout,"oec_json: archive MD5 progress %llu files\n",(unsigned long long)n);std::fflush(stdout);}}
  uint64_t missing=0; for(size_t i=0;i<files.size();++i)if(files[i].type=="file"){std::string k=oec_norm_relpath(files[i].path);std::map<std::string,std::string>::const_iterator it=hashes.find(k);if(it==hashes.end()||it->second.empty()){++missing;continue;}files[i].md5=it->second;files[i].md5_source="archive-extract";}
  oec_remove_tree(temp); if(missing){std::ostringstream e;e<<"--force-md5 could not match "<<missing<<" extracted files to catalog paths";err=e.str();return false;} return true;
}

inline bool oec_merge_progressive_md5(const std::string& json_path,const std::map<std::string,std::string>& source_hashes,std::vector<OecJsonFileRecord>& files,std::string& err){
  std::map<std::string,OecJsonFileRecord> old; if(path_exists(json_path) && !oec_load_existing_json_records(json_path,old,err))return false;
  for(size_t i=0;i<files.size();++i){if(files[i].type!="file")continue;std::string k=oec_norm_relpath(files[i].path);std::map<std::string,std::string>::const_iterator nh=source_hashes.find(k);
    if(nh==source_hashes.end()){nh=source_hashes.find(oec_basename(k));}
    if(nh!=source_hashes.end()&&!nh->second.empty()){files[i].md5=nh->second;files[i].md5_source="source";continue;}
    std::map<std::string,OecJsonFileRecord>::const_iterator oi=old.find(k); if(oi!=old.end()&&!oi->second.md5.empty() && oi->second.size==files[i].size && oi->second.modified==files[i].modified){files[i].md5=oi->second.md5;files[i].md5_source=oi->second.md5_source.empty()?"preserved":oi->second.md5_source;}
  } return true;
}

inline void oec_json_usage() {
  std::fprintf(stderr,
    "OEC JSON file catalog:\n"
    "  oec_json ARCHIVE [--force-md5] [--digits N] [--oec-index PATH] [--idx PATH] [--no-idx] [--idx-plaintext] [-key PASSWORD]\n"
    "  alias: oec_j\n\n"
    "Output is fixed beside the archive and is NEVER overwritten:\n"
    "  aaa???.zpaq -> aaa.json\n"
    "  bbb.zpaq    -> bbb.json\n"
    "Direct oec_json never overwrites; oec_a may progressively refresh an existing catalog.\n");
}

inline int oec_json_command(int argc, const char* const* argv) {
  if(argc<3) { oec_json_usage(); return 2; }
  const std::string exe=argv[0], spec=argv[2];
  uint32_t digits=3; std::string index_override, idx_override; bool use_idx=true, idx_plaintext=false, force_md5=false;
  std::vector<std::string> auth;
  for(int i=3;i<argc;++i) {
    const std::string a=argv[i];
    if(a=="--digits" && i+1<argc) { if(!zfec::parse_u32(argv[++i],digits)||digits<1||digits>9){std::fprintf(stderr,"oec_json: bad --digits\n");return 2;} }
    else if(a=="--oec-index" && i+1<argc) index_override=argv[++i];
    else if(a=="--idx" && i+1<argc) { idx_override=argv[++i]; use_idx=true; }
    else if(a=="--no-idx") use_idx=false;
    else if(a=="--idx-plaintext") { idx_plaintext=true; use_idx=true; }
    else if(a=="--force-md5") force_md5=true;
    else if((a=="-key" || a=="-franzen") && i+1<argc) { auth.push_back(a); auth.push_back(argv[++i]); }
    else { std::fprintf(stderr,"oec_json: unknown option %s\n",a.c_str()); return 2; }
  }
  const std::string output=oec_json_output_path(spec);
  if(output.empty() || output==".json") { std::fprintf(stderr,"oec_json: cannot infer output JSON path\n"); return 2; }
  // Deliberately check BEFORE opening/parsing the archive. Existing catalogs are immutable by default.
  if(path_exists(output)) { std::fprintf(stderr,"oec_json: output already exists, refusing to overwrite: %s\n",output.c_str()); return 3; }

  OecReadLayout layout; std::string err;
  if(!resolve_oec_read_layout(spec,digits,index_override,layout,err)){std::fprintf(stderr,"oec_json: %s\n",err.c_str());return 2;}
  if(!path_exists(layout.index)){std::fprintf(stderr,"oec_json: OEC zero-part index not found: %s (run oecinit first)\n",layout.index.c_str());return 4;}
  const std::string idx_path=idx_override.empty()?default_idx_for_layout(spec,layout):idx_override;

  std::string listing, source_kind="zero-part-terse";
  bool got_from_idx=false;
  bool encrypted=false; std::string encerr;
  const bool enc_known=oec_file_looks_standard_aes_encrypted(layout.index,encrypted,encerr);
  if(use_idx && (!enc_known || !encrypted || idx_plaintext)) {
    oecidx::Cache cache;
    if(cache.open(idx_path,layout.index,err) && cache.has_list()) {
      listing.assign(cache.list_data(),cache.list_size());
      source_kind="idx-mmap"; got_from_idx=true;
    }
  }
  std::vector<OecJsonFileRecord> files;
  if(got_from_idx) oec_parse_file_list(listing,files);
  // Old/current IDX list text may be a human-oriented layout that is not parseable
  // enough for a lossless catalog. Fall back to one native terse pass in that case.
  if(!got_from_idx || files.empty()) {
    std::vector<std::string> args; args.push_back("l"); args.push_back(layout.index); args.push_back("-terse"); args.push_back("-nocolor");
    args.insert(args.end(),auth.begin(),auth.end());
    std::string caperr; int rc=spawn_self_capture(exe,args,listing,caperr,"oec_json native l -terse");
    if(rc!=0){std::fprintf(stderr,"oec_json: native terse list failed rc=%d%s%s\n",rc,caperr.empty()?"":" (",caperr.empty()?"":caperr.c_str());return 5;}
    oec_strip_auth_chatter(listing); files.clear(); oec_parse_file_list(listing,files); source_kind="zero-part-terse";
  }
  if(files.empty() && listing.find("0 files") == std::string::npos && listing.find("0 file") == std::string::npos) {
    // zpaqfranz -terse is not a stable machine format. Retry with -all, whose |status/version layout
    // is historically the most explicit, then collapse all versions/deletions to current state.
    std::vector<std::string> args; args.push_back("l"); args.push_back(layout.index); args.push_back("-all"); args.push_back("-terse"); args.push_back("-nocolor"); args.insert(args.end(),auth.begin(),auth.end());
    std::string all_listing,caperr; int rc=spawn_self_capture(exe,args,all_listing,caperr,"oec_json native l -all -terse");
    if(rc==0){oec_strip_auth_chatter(all_listing);std::vector<OecJsonFileRecord> all;oec_parse_file_list(all_listing,all);if(!all.empty()){oec_collapse_current_records(all);files.swap(all);listing.swap(all_listing);source_kind="zero-part-all-terse";}}
  }
  if(files.empty()) {
    if(listing.find("0 files") == std::string::npos && listing.find("0 file") == std::string::npos) {
      std::fprintf(stderr,"oec_json: could not parse file records from zpaqfranz list output after terse + all-terse fallbacks; JSON was not created\n");
      std::fprintf(stderr,"oec_json: hint: run native 'l <zero-part> -all -terse -nocolor' to inspect the local output layout\n"); return 6;
    }
  }
  if(force_md5) {
    if(!oec_force_md5_from_archive(exe,layout,auth,files,output,err)) { std::fprintf(stderr,"oec_json: %s\n",err.c_str()); return 7; }
    source_kind += "+archive-md5";
  }
  if(!oec_write_json_catalog(output,spec,layout.index,source_kind,files,err,false)) { std::fprintf(stderr,"oec_json: %s\n",err.c_str()); return 8; }
  uint64_t total=0; for(size_t i=0;i<files.size();++i) total+=files[i].size;
  std::fprintf(stdout,"oec_json: wrote %s (%llu files, %llu bytes, source=%s)\n",output.c_str(),
               (unsigned long long)files.size(),(unsigned long long)total,source_kind.c_str());
  return 0;
}

inline bool oec_progressive_json_after_add(const std::string& exe,const std::string& archive_spec,const std::string& index,
                                           const std::vector<std::string>& add_args,bool force_create,std::string& err,const OecIgnorePlan* ignore_plan){
  const std::string json=oec_json_output_path(archive_spec);
  const bool existed=path_exists(json);
  if(!existed && !force_create) return true;
  std::fprintf(stdout,"oec_a: progressive JSON %s %s\n",existed?"update":"create",json.c_str()); std::fflush(stdout);
  std::map<std::string,std::string> source_hashes;
  if(!oec_collect_source_md5(add_args,source_hashes,err,ignore_plan)) return false;
  std::vector<std::string> auth=oec_extract_auth_args(add_args), a; a.push_back("l");a.push_back(index);a.push_back("-terse");a.push_back("-nocolor");a.insert(a.end(),auth.begin(),auth.end());
  std::string listing,caperr; int rc=spawn_self_capture(exe,a,listing,caperr,"oec_a JSON metadata refresh");
  if(rc!=0){std::ostringstream e;e<<"native l for JSON refresh failed rc="<<rc; if(!caperr.empty())e<<" ("<<caperr<<")";err=e.str();return false;}
  oec_strip_auth_chatter(listing); std::vector<OecJsonFileRecord> files;oec_parse_file_list(listing,files);
  if(files.empty() && listing.find("0 files")==std::string::npos && listing.find("0 file")==std::string::npos){err="could not parse current file list for JSON refresh";return false;}
  if(!oec_merge_progressive_md5(json,source_hashes,files,err))return false;
  if(!oec_write_json_catalog(json,archive_spec,index,"oec_a-progressive",files,err,true))return false;
  uint64_t hashed=0;for(size_t i=0;i<files.size();++i)if(files[i].type=="file"&&!files[i].md5.empty())++hashed;
  std::fprintf(stdout,"oec_a: JSON updated %s (%llu records, %llu MD5 values)\n",json.c_str(),(unsigned long long)files.size(),(unsigned long long)hashed);std::fflush(stdout);
  return true;
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
    "  oec_a BASE SOURCE...            optimized add + EC; zpaq.ignore recursive filtering; -gitignore adds .gitignore\n"
    "  oec_l ARCHIVE [options...]      optimized list; metadata from .000 only\n"
    "  oec_i ARCHIVE [options...]      optimized info/versions; metadata from .000 only\n"
    "  oec_x ARCHIVE [files/options]   OEC equivalent of native x\n"
    "  oec_e ARCHIVE [files/options]   OEC equivalent of native e\n"
    "  oec_idx build|verify|info|drop  mmap SSD cache manager\n"
    "  oec_json | oec_j ARCHIVE       write JSON catalog; --force-md5 hashes extracted payload\n"
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
  if (cmd=="oec_json" || cmd=="oec_j") return oec_json_command(argc, argv);
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
