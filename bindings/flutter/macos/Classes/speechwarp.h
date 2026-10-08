// Lets the library's sources find their public header without a search path. Xcode works search paths out
// from the plugin's symlink in the app, where "../.." does not lead to the repository.
#if __has_include("../../src/speechwarp/include/speechwarp.h")
#include "../../src/speechwarp/include/speechwarp.h"
#else
#include "../../../../include/speechwarp.h"
#endif
