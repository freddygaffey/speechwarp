#import <Foundation/Foundation.h>
#import "SpeechwarpImpl.h"
#import <ReactCommon/CxxTurboModuleUtils.h>

@interface SpeechwarpOnLoad : NSObject
@end

@implementation SpeechwarpOnLoad

using namespace facebook::react;

+ (void)load
{
  registerCxxModuleToGlobalModuleMap(
    std::string(SpeechwarpImpl::kModuleName),
    [](std::shared_ptr<CallInvoker> jsInvoker) {
      return std::make_shared<SpeechwarpImpl>(jsInvoker);
    }
  );
}

@end
