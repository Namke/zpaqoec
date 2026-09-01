#include "zpaqfranz_ext.hpp"
int main(int argc, char** argv) {
  int rc = zfext::dispatch(argc, argv);
  if (rc == zfext::kNotHandled) {
    std::fprintf(stderr, "standalone test binary only supports ec/trunkadd\n");
    return 2;
  }
  return rc;
}
