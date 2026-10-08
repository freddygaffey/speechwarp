// CocoaPods compiles the files in this folder, so each of the library's sources is pulled in by one like this.
// The copy in ../../src/speechwarp/ (see ../../scripts/vendor.sh) is what a published package holds; in the
// repository, without that copy, the sources at its top are used.
#if __has_include("../../src/speechwarp/src/speechwarp.c")
#include "../../src/speechwarp/src/speechwarp.c"
#else
#include "../../../../src/speechwarp.c"
#endif
