// Paula's Revenge - native macOS full screen (green button behaviour from the menu)
#import <AppKit/AppKit.h>

void prvToggleNativeFullScreen (void* nsView);
void prvToggleNativeFullScreen (void* nsView)
{
    NSView* view = (NSView*) nsView;
    NSWindow* window = [view window];
    if (window == nil) return;
    [window setCollectionBehavior: [window collectionBehavior] | NSWindowCollectionBehaviorFullScreenPrimary];
    [window toggleFullScreen: nil];
}
