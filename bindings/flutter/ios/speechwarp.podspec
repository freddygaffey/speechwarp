#
# Builds the speechwarp C library into the app. The files in Classes/ include the sources, which are in
# ../src/speechwarp/ in a published package (copied in by ../scripts/vendor.sh) and at the top of the
# repository otherwise.
#
Pod::Spec.new do |s|
  s.name             = 'speechwarp'
  s.version          = '0.3.4'
  s.summary          = 'Nonlinear speed-up for speech.'
  s.description      = <<-DESC
Nonlinear speed-up for speech: listen faster and still follow it. Packages Google's Speedy algorithm and the
Sonic library. Not an official Google product.
                       DESC
  s.homepage         = 'https://github.com/fredgaffey/speechwarp'
  s.license          = { :type => 'Apache-2.0' }
  s.author           = { 'The speechwarp contributors' => 'noreply@github.com' }
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.dependency 'Flutter'
  s.platform = :ios, '13.0'

  # The headers in Classes/ only forward to the library's own; they are not for apps to import.
  s.project_header_files = 'Classes/*.h'

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'NO',
    'HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/Classes"',
    # Upstream is full of assertions, and its warnings are not ours to fix.
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) NDEBUG=1',
    'GCC_WARN_INHIBIT_ALL_WARNINGS' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386'
  }
end
