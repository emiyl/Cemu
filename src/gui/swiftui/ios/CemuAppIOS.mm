#include "Common/precompiled.h"
#include "config/ActiveSettings.h"
#include "gui/swiftui/macos/CemuApp.h"
#include <TargetConditionals.h>

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

    NSArray<NSURL *> *docURLs =
        [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask];
    NSURL *documentsURL = docURLs.firstObject;

    NSURL *cemuDataURL = [documentsURL URLByAppendingPathComponent:@"Cemu"];
    [fm createDirectoryAtURL:cemuDataURL
        withIntermediateDirectories:YES
                         attributes:nil
                              error:nil];

    user_data_path = config_path = fs::path([[cemuDataURL path] UTF8String]);

    NSURL *cemuCacheURL = [cemuDataURL URLByAppendingPathComponent:@"cache"];
    [fm createDirectoryAtURL:cemuCacheURL
        withIntermediateDirectories:YES
                         attributes:nil
                              error:nil];

    cache_path = fs::path([[cemuCacheURL path] UTF8String]);

    NSString *resourcePath = [[NSBundle mainBundle] resourcePath];
    data_path = fs::path([resourcePath UTF8String]);
  }

  ActiveSettings::SetPaths(false, exePath, user_data_path, config_path,
                           cache_path, data_path, failedWriteAccess);
}

#endif
