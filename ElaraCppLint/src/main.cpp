#include "cpplint/CppLint.h"

using namespace elara;

int main(int argc, const char *argv[]) {
    CppLint lint;
    return lint.run(argc, argv);
}
