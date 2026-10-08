// CocoaPods compiles the files in this folder, so each of the library's sources is pulled in by one like this.
// The copy in ../../src/speechwarp/ (see ../../scripts/vendor.sh) is what a published package holds; in the
// repository, without that copy, the sources at its top are used.
#if __has_include("../../src/speechwarp/src/third_party_speedy.c")
#include "../../src/speechwarp/src/third_party_speedy.c"
#else
#include "../../../../src/third_party_speedy.c"
#endif
