/* ZPAQOEC_BRIDGE_DECL */
int zfext_oec_dispatch_bridge(int argc, const char* const* argv);
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdlib>

static bool ex(const std::string& p){ FILE* f=fopen(p.c_str(),"rb"); if(!f)return false; fclose(f); return true; }
static bool splitpat(const std::string& pat, std::string& prefix, std::string& suffix, unsigned& digits) {
  size_t q=pat.find('?'); if(q==std::string::npos) return false;
  size_t e=q; while(e<pat.size() && pat[e]=='?') ++e;
  digits=(unsigned)(e-q); prefix=pat.substr(0,q); suffix=pat.substr(e); return digits>0;
}
static std::string pnum(const std::string& prefix,const std::string& suffix,unsigned digits,unsigned n){
  std::ostringstream s; s<<prefix<<std::setw(digits)<<std::setfill('0')<<n<<suffix; return s.str();
}
static void log_call(int argc, const char* const* argv) {
  FILE* f=fopen("native_calls.log","ab"); if(!f) return;
  for(int i=1;i<argc;++i) std::fprintf(f,"%s%s", i==1?"":"|", argv[i]);
  std::fprintf(f,"\n"); fclose(f);
}


static bool fake_encrypted(){ const char* e=std::getenv("FAKE_ENCRYPTED"); return e && *e && std::strcmp(e,"0")!=0; }
static void fake_index_header(FILE* f){ if(std::ftell(f)==0) std::fputs(fake_encrypted()?"Xfake-index\n":"zfake-index\n",f); }
static bool fake_auth_if_needed(const std::string& arc){
  (void)arc;
  if(!fake_encrypted()) return true;
  const char* fk=std::getenv("FRANZKEY");
  if(fk && *fk) { std::printf("Archive is AES-encrypted\n(fake FRANZKEY accepted)\n"); std::fflush(stdout); return true; }
  std::printf("Archive is AES-encrypted\n\nEnter AES password: "); std::fflush(stdout);
  char pw[256]; if(!std::fgets(pw,sizeof(pw),stdin)) return false;
  std::printf("****************\n"); std::fflush(stdout); return true;
}

// Deliberate decoys: real zpaqfranz has platform-conditional entry points.
// A no-argument main must NEVER receive an argc/argv bridge call.
#if 0
int main() { return 98; }
#endif
// Parameterized mains are eligible even when conditionally compiled out.
#if 0
int main(int argc, char** argv) {
  /* ZPAQFRANZ_OEC_DISPATCH */
  { const int zfext_rc = zfext_oec_dispatch_bridge(argc, argv); if (zfext_rc != -777777) return zfext_rc; }


 return 99; }
#endif
// Multiple internal parser definitions may exist in conditional branches; all
// eligible textual definitions should be instrumented.
#if 0
int zpaq_main_internal(int argc, const char** argv) {
  /* ZPAQFRANZ_OEC_DISPATCH */
  { const int zfext_rc = zfext_oec_dispatch_bridge(argc, argv); if (zfext_rc != -777777) return zfext_rc; }


 return argc + (argv ? 90 : 0); }
#endif

int zpaq_main_internal(int argc, const char** argv) {
  /* ZPAQFRANZ_OEC_DISPATCH */
  { const int zfext_rc = zfext_oec_dispatch_bridge(argc, argv); if (zfext_rc != -777777) return zfext_rc; }



  if(argc<3) { std::fprintf(stderr,"fake upstream: missing args\n"); return 2; }
  log_call(argc, argv);
  std::string cmd=argv[1], arc=argv[2], prefix,suffix; unsigned digits=0;
  std::string idx;
  for(int i=3;i+1<argc;++i) if(std::string(argv[i])=="-index") idx=argv[i+1];

  if(cmd=="a") {
    for(int ai=3;ai+1<argc;++ai) if(std::string(argv[ai])=="-exclude") {
      FILE* in=fopen(argv[ai+1],"rb"); FILE* out=fopen("fake_exclude_seen.txt","wb");
      if(!in||!out){if(in)fclose(in);if(out)fclose(out);return 33;} char b[4096]; size_t n; while((n=fread(b,1,sizeof(b),in))>0)fwrite(b,1,n,out); fclose(in); fclose(out);
    }
    if(idx.empty()) return 4;
    if(splitpat(arc,prefix,suffix,digits)) {
      unsigned n=1;
      for(;;++n){ std::string p=pnum(prefix,suffix,digits,n); if(!ex(p)) { arc=p; break; } }
      FILE* f=fopen(arc.c_str(),"wb"); if(!f)return 5;
      for(int i=0;i<3*1024*1024+123;++i) fputc((i*17+n*31)&255,f); fclose(f);
      FILE* ix=fopen(idx.c_str(),"ab"); if(!ix)return 6; fake_index_header(ix); std::fprintf(ix,"part=%u %s\n",n,arc.c_str()); fclose(ix);
      std::printf("fake add wrote %s and %s\n",arc.c_str(),idx.c_str());
      return 0;
    }
    if(!ex(arc) || !ex(idx)) return 3;
    FILE* f=fopen(arc.c_str(),"ab"); if(!f)return 5;
    for(int i=0;i<512*1024+77;++i) fputc((i*19+41)&255,f); fclose(f);
    FILE* ix=fopen(idx.c_str(),"ab"); if(!ix)return 6; fake_index_header(ix); std::fprintf(ix,"single-update %s\n",arc.c_str()); fclose(ix);
    std::printf("fake single add updated %s and %s\n",arc.c_str(),idx.c_str());
    return 0;
  }

  if(cmd=="x" && !idx.empty()) {
    if(!fake_auth_if_needed(arc)) return 31;
    FILE* ix=fopen(idx.c_str(),"wb"); if(!ix)return 6; fake_index_header(ix);
    if(splitpat(arc,prefix,suffix,digits)) {
      unsigned count=0;
      for(unsigned n=1;;++n){
        std::string p=pnum(prefix,suffix,digits,n); if(!ex(p)) break;
        std::fprintf(ix,"part=%u %s\n",n,p.c_str()); ++count;
      }
      fclose(ix);
      if(!count) { std::remove(idx.c_str()); return 7; }
      std::printf("fake extract-index rebuilt %s from %u parts\n",idx.c_str(),count);
      return 0;
    }
    if(!ex(arc) || arc.find("failindex")!=std::string::npos) { fclose(ix); std::remove(idx.c_str()); return 7; }
    std::fprintf(ix,"single=%s\n",arc.c_str()); fclose(ix);
    std::printf("fake extract-index rebuilt %s from single %s\n",idx.c_str(),arc.c_str());
    return 0;
  }

  if(cmd=="l" || cmd=="i") {
    if(!fake_auth_if_needed(arc)) return 32;
    if(arc.find('?')!=std::string::npos) { std::fprintf(stderr,"fake metadata command was given multipart pattern\n"); return 20; }
    if(!ex(arc)) return 21;
    if(cmd=="l") {
      const char* fl=std::getenv("FAKE_LIST_LAYOUT"); std::string layout=fl?fl:"pipe"; bool all=false; for(int ai=3;ai<argc;++ai)if(std::string(argv[ai])=="-all")all=true;
      std::printf("<<%s>>: 1 versions, 2 files, 1.234 bytes\n",arc.c_str());
      if(layout=="plain") {
        std::printf("2026/08/31 123456 0644 1,234 87%% 1 folder/file one.txt\n");
        std::printf("2026/08/31 123500 d0755 0 0%% 1 folder/subdir\n");
      } else if(layout=="allonly" && !all) {
        std::printf("CURRENT-LIST-LAYOUT-UNKNOWN folder/file one.txt\n");
      } else if(layout=="allonly") {
        std::printf("2026-08-30 10:00:00 0644 100 90%% 1|+ folder/obsolete.txt\n");
        std::printf("deleted/inacessible 0 del 2|- folder/obsolete.txt\n");
        std::printf("2026-08-31 12:34:56 0644 1.234 87%% 3|+ folder/file one.txt\n");
        std::printf("2026-08-31 12:35:00 d0755 0 0%% 3|+ folder/subdir\n");
      } else {
        std::printf("2026-08-31 12:34:56 0644 1.234 87%% 0001|+ folder/file one.txt\n");
        std::printf("2026-08-31 12:35:00 d0755 0 0%% 0001|+ folder/subdir\n");
      }
    } else {
      std::printf("fake i metadata from %s\n",arc.c_str());
    }
    return 0;
  }

  if(cmd=="x" || cmd=="e") {
    if(splitpat(arc,prefix,suffix,digits)) {
      if(!ex(pnum(prefix,suffix,digits,1))) return 23;
    } else if(!ex(arc)) {
      std::fprintf(stderr,"fake extract command data not found\n"); return 22;
    }
    std::string to; for(int i=3;i+1<argc;++i) if(std::string(argv[i])=="-to") to=argv[i+1];
    if(!to.empty()) {
#ifdef _WIN32
      std::string mk="mkdir \""+to+"\\folder\\subdir\" >nul 2>nul";
#else
      std::string mk="mkdir -p \""+to+"/folder/subdir\"";
#endif
      std::system(mk.c_str()); std::string fp=to+"/folder/file one.txt"; FILE* out=fopen(fp.c_str(),"wb"); if(!out)return 24; for(int i=0;i<1234;++i)fputc((i*11+7)&255,out); fclose(out);
    }
    std::printf("fake %s payload from %s\n",cmd.c_str(),arc.c_str());
    return 0;
  }

  std::fprintf(stderr,"fake upstream unsupported command: %s\n",cmd.c_str()); return 2;
}

int main(int argc, char** argv) {
  /* ZPAQFRANZ_OEC_DISPATCH */
  { const int zfext_rc = zfext_oec_dispatch_bridge(argc, argv); if (zfext_rc != -777777) return zfext_rc; }



  return zpaq_main_internal(argc, const_cast<const char**>(argv));
}



#include "extensions/zpaqfranz_ext.hpp"
/* ZPAQOEC_BRIDGE_DEF */
int zfext_oec_dispatch_bridge(int argc, const char* const* argv) { return zfext::dispatch_const(argc, argv); }
