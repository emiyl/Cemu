#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <Cocoa/Cocoa.h>
#endif
#import <QuartzCore/CAMetalLayer.h>

#if TARGET_OS_IOS
@interface MetalView : UIView
#else
@interface MetalView : NSView
#endif
@end
