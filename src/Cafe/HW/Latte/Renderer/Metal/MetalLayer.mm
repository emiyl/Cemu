#include "Cafe/HW/Latte/Renderer/Metal/MetalLayer.h"

#include "Cafe/HW/Latte/Renderer/MetalView.h"

#if TARGET_OS_IOS
void ConfigureMetalLayerForIOS(void* layerPtr)
{
	CAMetalLayer* layer = (CAMetalLayer*)layerPtr;
	// Disable display-sync so drawables are returned to the pool as soon as the
	// GPU finishes rather than waiting for the next screen scan-out.  Without
	// this, the pool (default size 3) exhausts within 3 frames and nextDrawable()
	// blocks the Latte thread forever, starving the vsync timer.
	layer.maximumDrawableCount = 2;
}
#endif

void* CreateMetalLayer(void* handle, float& scaleX, float& scaleY)
{
	#if TARGET_OS_IOS
	UIView* view = (UIView*)handle;

	MetalView* childView = [[MetalView alloc] initWithFrame:view.bounds];
	childView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

	[view addSubview:childView];

	scaleX = childView.contentScaleFactor;
	scaleY = childView.contentScaleFactor;

	return childView.layer;
	#else
	NSView* view = (NSView*)handle;

	MetalView* childView = [[MetalView alloc] initWithFrame:view.bounds];
	childView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	childView.wantsLayer = YES;

	[view addSubview:childView];

	const NSRect points = [childView frame];
    const NSRect pixels = [childView convertRectToBacking:points];

	scaleX = (float)(pixels.size.width / points.size.width);
    scaleY = (float)(pixels.size.height / points.size.height);

	return childView.layer;
	#endif
}
