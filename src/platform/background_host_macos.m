#include "platform/background_host.h"

#import <AppKit/AppKit.h>

#include <stdlib.h>

struct gdox_background_host {
    NSStatusItem *status_item;
    NSObject *delegate;
    gdox_background_host_event pending;
    bool termination_pending;
};

@interface GDOXBackgroundDelegate : NSObject <NSApplicationDelegate>
{
    gdox_background_host *_host;
    id _forward_delegate;
}
- (id)initWithHost:(gdox_background_host *)host;
- (void)installAsApplicationDelegate;
- (void)detachApplicationDelegate;
- (void)openGDOX:(id)sender;
- (void)quitGDOX:(id)sender;
@end

@implementation GDOXBackgroundDelegate
- (id)initWithHost:(gdox_background_host *)host
{
    self = [super init];
    if (self != nil) {
        _host = host;
    }
    return self;
}

- (void)dealloc
{
    [_forward_delegate release];
    [super dealloc];
}

- (void)installAsApplicationDelegate
{
    id current = [NSApp delegate];

    if (current == self) {
        return;
    }
    [current retain];
    [_forward_delegate release];
    _forward_delegate = current;
    [NSApp setDelegate:self];
}

- (void)detachApplicationDelegate
{
    if ([NSApp delegate] == self) {
        [NSApp setDelegate:_forward_delegate];
    }
}

- (BOOL)respondsToSelector:(SEL)selector
{
    return [super respondsToSelector:selector]
        || [_forward_delegate respondsToSelector:selector];
}

- (id)forwardingTargetForSelector:(SEL)selector
{
    if ([_forward_delegate respondsToSelector:selector]) {
        return _forward_delegate;
    }
    return [super forwardingTargetForSelector:selector];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication *)sender
{
    (void)sender;
    _host->termination_pending = true;
    _host->pending = GDOX_BACKGROUND_HOST_QUIT;
    return NSTerminateLater;
}

- (void)openGDOX:(id)sender
{
    (void)sender;
    _host->pending = GDOX_BACKGROUND_HOST_OPEN;
}

- (void)quitGDOX:(id)sender
{
    (void)sender;
    _host->pending = GDOX_BACKGROUND_HOST_QUIT;
}
@end

gdox_background_host *gdox_background_host_create(void)
{
    @autoreleasepool {
        gdox_background_host *host = calloc(1U, sizeof(*host));
        GDOXBackgroundDelegate *delegate;
        NSMenu *menu;
        NSMenuItem *open_item;
        NSMenuItem *quit_item;

        if (host == NULL) {
            return NULL;
        }
        (void)[NSApplication sharedApplication];
        delegate = [[GDOXBackgroundDelegate alloc] initWithHost:host];
        host->delegate = delegate;
        [delegate installAsApplicationDelegate];
        host->status_item = [
            [NSStatusBar systemStatusBar]
            statusItemWithLength:NSVariableStatusItemLength
        ];
        [host->status_item retain];
        if (host->status_item == nil || host->status_item.button == nil) {
            [host->status_item release];
            [delegate detachApplicationDelegate];
            [delegate release];
            free(host);
            return NULL;
        }
        host->status_item.button.title = @"GDOX";
        host->status_item.button.toolTip = @"GDOX";

        menu = [[NSMenu alloc] initWithTitle:@"GDOX"];
        open_item = [[NSMenuItem alloc]
            initWithTitle:@"Open GDOX"
            action:@selector(openGDOX:)
            keyEquivalent:@""];
        open_item.target = delegate;
        [menu addItem:open_item];
        [menu addItem:[NSMenuItem separatorItem]];
        quit_item = [[NSMenuItem alloc]
            initWithTitle:@"Quit"
            action:@selector(quitGDOX:)
            keyEquivalent:@"q"];
        quit_item.target = delegate;
        [menu addItem:quit_item];
        host->status_item.menu = menu;
        [open_item release];
        [quit_item release];
        [menu release];
        return host;
    }
    return NULL;
}

gdox_background_host_event gdox_background_host_poll(
    gdox_background_host *host,
    bool background_only
)
{
    if (host == NULL) {
        return GDOX_BACKGROUND_HOST_NONE;
    }
    @autoreleasepool {
        NSEvent *event;
        gdox_background_host_event pending;

        if (background_only) {
            while ((event = [NSApp
                nextEventMatchingMask:NSEventMaskAny
                untilDate:[NSDate distantPast]
                inMode:NSDefaultRunLoopMode
                dequeue:YES]) != nil) {
                [NSApp sendEvent:event];
            }
            [NSApp updateWindows];
        }
        pending = host->pending;
        host->pending = GDOX_BACKGROUND_HOST_NONE;
        return pending;
    }
    return GDOX_BACKGROUND_HOST_NONE;
}

void gdox_background_host_set_status(
    gdox_background_host *host,
    const char *status
)
{
    if (host == NULL || host->status_item.button == nil) {
        return;
    }
    @autoreleasepool {
        NSString *detail = status != NULL && status[0] != '\0'
            ? [NSString stringWithUTF8String:status]
            : @"Ready";
        if (detail == nil) {
            detail = @"Ready";
        }
        host->status_item.button.toolTip = [NSString
            stringWithFormat:@"GDOX - %@", detail];
    }
}

void gdox_background_host_set_window_visible(
    gdox_background_host *host,
    bool visible
)
{
    if (host == NULL) {
        return;
    }
    @autoreleasepool {
        [(GDOXBackgroundDelegate *)host->delegate
            installAsApplicationDelegate];
        [NSApp setActivationPolicy:
            visible
                ? NSApplicationActivationPolicyRegular
                : NSApplicationActivationPolicyAccessory];
        if (visible) {
            [NSApp activateIgnoringOtherApps:YES];
        }
    }
}

void gdox_background_host_complete_shutdown(gdox_background_host *host)
{
    if (host == NULL || !host->termination_pending) {
        return;
    }
    host->termination_pending = false;
    [NSApp replyToApplicationShouldTerminate:YES];
}

void gdox_background_host_destroy(gdox_background_host *host)
{
    if (host == NULL) {
        return;
    }
    @autoreleasepool {
        [(GDOXBackgroundDelegate *)host->delegate
            detachApplicationDelegate];
        if (host->status_item != nil) {
            [[NSStatusBar systemStatusBar] removeStatusItem:host->status_item];
            [host->status_item release];
        }
        [host->delegate release];
        free(host);
    }
}
