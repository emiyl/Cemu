#include "Common/precompiled.h"
#include "config/ActiveSettings.h"
#include "gui/swiftui/macos/CemuApp.h"

#if TARGET_OS_IOS

#import <Foundation/Foundation.h>
#include <cstdio>
#include <cstdlib>
#include <mach-o/dyld.h>
#include <vector>

void CemuApp::DeterminePaths(std::set<fs::path> &failedWriteAccess) {
  std::error_code ec;

  fs::path exePath;
  fs::path user_data_path;
  fs::path config_path;
  fs::path cache_path;
  fs::path data_path;

  @autoreleasepool {
    NSFileManager *fm = [NSFileManager defaultManager];

    uint32_t exePathBufferSize = 0;
    _NSGetExecutablePath(nullptr, &exePathBufferSize);
    std::vector<char> exePathBuffer(exePathBufferSize, '\0');

    if (_NSGetExecutablePath(exePathBuffer.data(), &exePathBufferSize) != 0) {
      fprintf(stderr, "Failed to resolve executable path\n");
      exit(0);
    }

    exePath = fs::weakly_canonical(fs::path(exePathBuffer.data()), ec);
    if (ec)
      exePath = fs::path(exePathBuffer.data());

    NSArray *appSupportPaths = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);

    NSString *appSupport = [appSupportPaths firstObject];
    NSString *cemuSupport = [appSupport stringByAppendingPathComponent:@"Cemu"];

    [fm createDirectoryAtPath:cemuSupport
        withIntermediateDirectories:YES
                         attributes:nil
                              error:nil];

    user_data_path = config_path = fs::path([cemuSupport UTF8String]);

    NSArray *cachePaths = NSSearchPathForDirectoriesInDomains(
        NSCachesDirectory, NSUserDomainMask, YES);

    NSString *cacheDir = [cachePaths firstObject];
    NSString *cemuCache = [cacheDir stringByAppendingPathComponent:@"Cemu"];

    [fm createDirectoryAtPath:cemuCache
        withIntermediateDirectories:YES
                         attributes:nil
                              error:nil];

    cache_path = fs::path([cemuCache UTF8String]);

    NSString *resourcePath = [[NSBundle mainBundle] resourcePath];
    data_path = fs::path([resourcePath UTF8String]);
  }

  ActiveSettings::SetPaths(false, exePath, user_data_path, config_path,
                           cache_path, data_path, failedWriteAccess);
}

#endif
