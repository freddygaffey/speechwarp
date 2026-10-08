require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

Pod::Spec.new do |s|
  s.name         = "Speechwarp"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => "https://github.com/fredgaffey/speechwarp.git", :tag => "#{s.version}" }

  # cpp/speechwarp holds the C library's sources, copied in by scripts/vendor.sh. Only its src/ is compiled:
  # those files include the ones in third_party/ themselves.
  s.source_files = "ios/**/*.{h,m,mm}", "cpp/*.{hpp,cpp,h}", "cpp/speechwarp/src/*.{c,h}",
                   "cpp/speechwarp/include/*.h", "ios/generated/*.{h,cpp,mm}"
  s.private_header_files = "ios/**/*.h", "cpp/speechwarp/**/*.h"
  s.preserve_paths = "cpp/speechwarp/third_party/**/*", "cpp/speechwarp/licenses/*"

  s.pod_target_xcconfig = {
    "HEADER_SEARCH_PATHS" => "\"$(PODS_TARGET_SRCROOT)/cpp/speechwarp/include\" \"$(PODS_TARGET_SRCROOT)/cpp/speechwarp/third_party/kissfft\"",
    # Upstream is full of assertions.
    "GCC_PREPROCESSOR_DEFINITIONS" => "$(inherited) NDEBUG=1"
  }

  install_modules_dependencies(s)
end
