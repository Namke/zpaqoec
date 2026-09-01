#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

static bool ex(const std::string& p){ FILE* f=fopen(p.c_str(),"rb"); if(!f)return false; fclose(f); return true; }
static bool splitpat(const std::string& pat, std::string& prefix, std::string& suffix, unsigned& digits) {
  size_t q=pat.find('?'); if(q==std::string::npos) return false;
  size_t e=q; while(e<pat.size() && pat[e]=='?') ++e;
  digits=(unsigned)(e-q); prefix=pat.substr(0,q); suffix=pat.substr(e); return digits>0;
}
static std::string pnum(const std::string& prefix,const std::string& suffix,unsigned digits,unsigned n){
  std::ostringstream s; s<<prefix<<std::setw(digits)<<std::setfill('0')<<n<<suffix; return s.str();
}
int main(int argc, char** argv) {
  if(argc<3) { std::fprintf(stderr,"fake upstream: missing args\n"); return 2; }
  std::string cmd=argv[1], pat=argv[2], prefix,suffix; unsigned digits=0;
  if(!splitpat(pat,prefix,suffix,digits)) return 3;
  std::string idx;
  for(int i=3;i+1<argc;++i) if(std::string(argv[i])=="-index") idx=argv[i+1];
  if(idx.empty()) return 4;

  if(cmd=="a") {
    unsigned n=1;
    for(;;++n){ std::string p=pnum(prefix,suffix,digits,n); if(!ex(p)) { pat=p; break; } }
    FILE* f=fopen(pat.c_str(),"wb"); if(!f)return 5;
    for(int i=0;i<3*1024*1024+123;++i) fputc((i*17+n*31)&255,f); fclose(f);
    FILE* ix=fopen(idx.c_str(),"ab"); if(!ix)return 6; std::fprintf(ix,"part=%u %s\n",n,pat.c_str()); fclose(ix);
    std::printf("fake add wrote %s and %s\n",pat.c_str(),idx.c_str());
    return 0;
  }

  if(cmd=="x") {
    FILE* ix=fopen(idx.c_str(),"wb"); if(!ix)return 6;
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

  std::fprintf(stderr,"fake upstream: only a/x supported\n"); return 2;
}
