#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

static bool ex(const std::string& p){ FILE* f=fopen(p.c_str(),"rb"); if(!f)return false; fclose(f); return true; }
int main(int argc, char** argv) {
  if(argc<3 || std::string(argv[1])!="a") { std::fprintf(stderr,"fake upstream: only a supported\n"); return 2; }
  std::string pat=argv[2];
  size_t q=pat.find('?'); if(q==std::string::npos) return 3;
  size_t e=q; while(e<pat.size() && pat[e]=='?') ++e;
  unsigned digits=(unsigned)(e-q);
  std::string prefix=pat.substr(0,q), suffix=pat.substr(e);
  unsigned n=1;
  for(;;++n){ std::ostringstream s; s<<prefix<<std::setw(digits)<<std::setfill('0')<<n<<suffix; if(!ex(s.str())) { pat=s.str(); break; } }
  std::string idx;
  for(int i=3;i+1<argc;++i) if(std::string(argv[i])=="-index") idx=argv[i+1];
  if(idx.empty()) return 4;
  FILE* f=fopen(pat.c_str(),"wb"); if(!f)return 5;
  for(int i=0;i<3*1024*1024+123;++i) fputc((i*17+n*31)&255,f); fclose(f);
  FILE* ix=fopen(idx.c_str(),"ab"); if(!ix)return 6; std::fprintf(ix,"part=%u %s\n",n,pat.c_str()); fclose(ix);
  std::printf("fake add wrote %s and %s\n",pat.c_str(),idx.c_str());
  return 0;
}
