#include "app.hpp"
#include "text.hpp"

#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

bool isMount(const std::string& path) {
  if (path == "/") return true;
  struct stat self;
  struct stat parent;
  if (stat(path.c_str(), &self) != 0) return false;
  if (stat((path + "/..").c_str(), &parent) != 0) return false;
  return self.st_dev != parent.st_dev;
}

void addVolume(std::vector<VolumeEntry>& out, const std::string& path, const std::string& label) {
  for (const VolumeEntry& existing : out) {
    if (existing.path == path) return;
  }
  VolumeEntry entry;
  entry.path = path;
  entry.label = label.empty() ? path : label;
  struct statvfs vfs;
  if (statvfs(path.c_str(), &vfs) == 0) {
    entry.total = static_cast<uint64_t>(vfs.f_blocks) * static_cast<uint64_t>(vfs.f_frsize);
    entry.free = static_cast<uint64_t>(vfs.f_bavail) * static_cast<uint64_t>(vfs.f_frsize);
  }
  out.push_back(entry);
}

void addMounts(std::vector<VolumeEntry>& out, const std::string& directory) {
  DIR* dir = opendir(directory.c_str());
  if (!dir) return;
  while (dirent* entry = readdir(dir)) {
    if (entry->d_name[0] == '.') continue;
    std::string path = directory + "/" + entry->d_name;
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    if (isMount(path)) addVolume(out, path, entry->d_name);
  }
  closedir(dir);
}

std::vector<VolumeEntry> listVolumes() {
  std::vector<VolumeEntry> out;
  addVolume(out, "/", zh::rootFolder());
  if (const char* home = std::getenv("HOME")) addVolume(out, home, zh::homeFolder());
  addMounts(out, "/Volumes");
  return out;
}

void clearEdges(Input& input) {
  input.leftPressed = false;
  input.rightPressed = false;
  input.keyBackspace = false;
  input.keyDelete = false;
  input.keyEnter = false;
  input.keyTab = false;
  input.keyEsc = false;
  input.keyLeft = false;
  input.keyRight = false;
  input.keyHome = false;
  input.keyEnd = false;
  input.keyF5 = false;
  input.keyUpNav = false;
  input.keyBackNav = false;
  input.keyForwardNav = false;
  input.copy = false;
  input.paste = false;
  input.selectAll = false;
  input.openShortcut = false;
  input.typed.clear();
}

}  // namespace

@interface SVView : NSView <NSTextInputClient, NSDraggingDestination>
- (void)scanPath:(NSString*)path;
@end

static SVView* gView = nil;

@implementation SVView {
  App* app_;
  Input input_;
  Host host_;
  bool scannedArg_;
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (!self) return nil;
  app_ = new App();
  scannedArg_ = false;
  gView = self;
  [self registerForDraggedTypes:@[NSPasteboardTypeFileURL, NSFilenamesPboardType]];
  __weak SVView* weakSelf = self;
  app_->setWake([weakSelf]() {
    dispatch_async(dispatch_get_main_queue(), ^{
      SVView* view = weakSelf;
      if (view) [view setNeedsDisplay:YES];
    });
  });
  host_.volumes = []() { return listVolumes(); };
  host_.pickFolder = [](std::string& path) {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.prompt = [NSString stringWithUTF8String:zh::pickTitle()];
    if ([panel runModal] != NSModalResponseOK || !panel.URL.path) return false;
    path = panel.URL.path.UTF8String;
    return !path.empty();
  };
  host_.pickSave = [](std::string& path) {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.allowedFileTypes = @[@"csv"];
    panel.nameFieldStringValue = @"usage.csv";
    if ([panel runModal] != NSModalResponseOK || !panel.URL.path) return false;
    path = panel.URL.path.UTF8String;
    return true;
  };
  host_.openPath = [](const std::string& path) {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]];
  };
  host_.revealPath = [](const std::string& path) {
    [[NSWorkspace sharedWorkspace] selectFile:[NSString stringWithUTF8String:path.c_str()] inFileViewerRootedAtPath:@""];
  };
  host_.setClipboard = [](const std::string& text) {
    NSPasteboard* board = [NSPasteboard generalPasteboard];
    [board clearContents];
    [board setString:[NSString stringWithUTF8String:text.c_str()] forType:NSPasteboardTypeString];
  };
  host_.getClipboard = []() {
    NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    return text ? std::string(text.UTF8String) : std::string();
  };
  host_.setTitle = [](const std::string&) {};
  return self;
}

- (void)dealloc {
  if (gView == self) gView = nil;
  delete app_;
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  if (!self.window) return;
  NSWindow* __weak weakWindow = self.window;
  host_.setTitle = [weakWindow](const std::string& title) {
    NSWindow* live = weakWindow;
    if (live) live.title = [NSString stringWithUTF8String:title.c_str()];
  };
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)scanPath:(NSString*)path {
  if (path.length == 0) return;
  app_->scanPath(path.UTF8String);
  [self setNeedsDisplay:YES];
}

- (void)updateMouse:(NSEvent*)event {
  NSPoint local = [self convertPoint:event.locationInWindow fromView:nil];
  CGFloat scale = self.window ? self.window.backingScaleFactor : 1;
  input_.mouseX = static_cast<int>(std::lround(local.x * scale));
  input_.mouseY = static_cast<int>(std::lround((NSHeight(self.bounds) - local.y) * scale));
}

- (void)mouseDown:(NSEvent*)event {
  [self.window makeFirstResponder:self];
  [self updateMouse:event];
  input_.leftPressed = true;
  [self setNeedsDisplay:YES];
  [self displayIfNeeded];
}

- (void)rightMouseUp:(NSEvent*)event {
  [self updateMouse:event];
  input_.rightPressed = true;
  [self setNeedsDisplay:YES];
  [self displayIfNeeded];
}

- (void)mouseMoved:(NSEvent*)event {
  [self updateMouse:event];
  [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event {
  [self updateMouse:event];
  [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent*)event {
  bool command = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
  bool control = (event.modifierFlags & NSEventModifierFlagControl) != 0 || command;
  bool option = (event.modifierFlags & NSEventModifierFlagOption) != 0;
  NSString* chars = event.charactersIgnoringModifiers.lowercaseString;
  input_.modCtrl = control;
  input_.modAlt = option;
  if (control && [chars isEqualToString:@"c"]) input_.copy = true;
  else if (control && [chars isEqualToString:@"v"]) input_.paste = true;
  else if (control && [chars isEqualToString:@"a"]) input_.selectAll = true;
  else if (control && [chars isEqualToString:@"o"]) input_.openShortcut = true;
  else if (event.keyCode == 96) input_.keyF5 = true;
  else {
    [self interpretKeyEvents:@[event]];
    return;
  }
  [self setNeedsDisplay:YES];
  [self displayIfNeeded];
}

- (void)insertText:(id)string replacementRange:(NSRange)range {
  NSString* text = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString*)string string] : string;
  if (text.UTF8String) input_.typed += text.UTF8String;
  [self setNeedsDisplay:YES];
  [self displayIfNeeded];
}

- (void)doCommandBySelector:(SEL)selector {
  if (selector == @selector(insertNewline:) || selector == @selector(insertLineBreak:)) input_.keyEnter = true;
  else if (selector == @selector(deleteBackward:)) input_.keyBackspace = true;
  else if (selector == @selector(deleteForward:)) input_.keyDelete = true;
  else if (selector == @selector(moveLeft:)) input_.keyLeft = true;
  else if (selector == @selector(moveRight:)) input_.keyRight = true;
  else if (selector == @selector(moveToBeginningOfLine:)) input_.keyHome = true;
  else if (selector == @selector(moveToEndOfLine:)) input_.keyEnd = true;
  else if (selector == @selector(insertTab:)) input_.keyTab = true;
  else if (selector == @selector(cancelOperation:)) input_.keyEsc = true;
  [self setNeedsDisplay:YES];
  [self displayIfNeeded];
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {}
- (void)unmarkText {}
- (BOOL)hasMarkedText { return NO; }
- (NSRange)markedRange { return NSMakeRange(NSNotFound, 0); }
- (NSRange)selectedRange { return NSMakeRange(NSNotFound, 0); }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange { return self.bounds; }
- (NSUInteger)characterIndexForPoint:(NSPoint)point { return 0; }
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText { return @[]; }
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
  return nil;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender { return NSDragOperationCopy; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
  NSString* path = nil;
  NSArray<NSURL*>* urls = [sender.draggingPasteboard readObjectsForClasses:@[[NSURL class]] options:nil];
  if (urls.count > 0) path = urls[0].path;
  if (!path) {
    NSArray* files = [sender.draggingPasteboard propertyListForType:NSFilenamesPboardType];
    if ([files isKindOfClass:[NSArray class]] && files.count > 0) path = files[0];
  }
  if (path) [self scanPath:path];
  return path != nil;
}

- (void)drawRect:(NSRect)dirtyRect {
  CGFloat scale = self.window ? self.window.backingScaleFactor : [[NSScreen mainScreen] backingScaleFactor];
  if (scale < 1) scale = 1;
  NSRect backing = [self convertRectToBacking:self.bounds];
  input_.windowW = std::max(1, static_cast<int>(std::lround(backing.size.width)));
  input_.windowH = std::max(1, static_cast<int>(std::lround(backing.size.height)));
  input_.dpi = static_cast<float>(96.0 * scale);
  input_.timeSec = [NSDate timeIntervalSinceReferenceDate];
  Frame frame;
  app_->update(input_, host_, frame);
  clearEdges(input_);

  if (frame.width > 0 && frame.height > 0 && !frame.bgra.empty()) {
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        nullptr, frame.bgra.data(), frame.bgra.size() * sizeof(uint32_t), nullptr);
    CGImageRef image = CGImageCreate(frame.width, frame.height, 8, 32, frame.width * 4, space,
                                     kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, provider, nullptr,
                                     false, kCGRenderingIntentDefault);
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, 0, NSHeight(self.bounds));
    CGContextScaleCTM(ctx, 1, -1);
    CGContextDrawImage(ctx, CGRectMake(0, 0, NSWidth(self.bounds), NSHeight(self.bounds)), image);
    CGContextRestoreGState(ctx);
    CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(space);

    for (const Frame::Text& text : frame.texts) {
      NSString* string = [NSString stringWithUTF8String:text.utf8.c_str()];
      if (!string) continue;
      NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
      style.lineBreakMode = NSLineBreakByTruncatingTail;
      style.alignment = text.center ? NSTextAlignmentCenter : NSTextAlignmentLeft;
      NSFont* font = [NSFont systemFontOfSize:std::max(8.0, text.fontPx / scale)];
      NSColor* color = [NSColor colorWithCalibratedRed:((text.rgb >> 16) & 255) / 255.0
                                                 green:((text.rgb >> 8) & 255) / 255.0
                                                  blue:(text.rgb & 255) / 255.0
                                                 alpha:1];
      NSDictionary* attrs = @{
        NSFontAttributeName : font,
        NSForegroundColorAttributeName : color,
        NSParagraphStyleAttributeName : style
      };
      CGFloat x = text.x / scale;
      CGFloat y = NSHeight(self.bounds) - (text.y + text.h) / scale;
      [string drawInRect:NSMakeRect(x, y, text.w / scale, text.h / scale) withAttributes:attrs];
    }
  }
  if (app_->animating()) {
    __weak SVView* weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 16 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      SVView* view = weakSelf;
      if (view) [view setNeedsDisplay:YES];
    });
  }
}

@end

@interface SVDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) int argc;
@property(nonatomic, assign) char** argv;
@end

@implementation SVDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  for (int i = 1; i < self.argc; ++i) {
    if (self.argv[i][0] != '-') {
      [gView scanPath:[NSString stringWithUTF8String:self.argv[i]]];
      break;
    }
  }
}
- (BOOL)application:(NSApplication*)sender openFile:(NSString*)filename {
  if (!gView) return NO;
  [gView scanPath:filename];
  return YES;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }
@end

int spaceviewer_main(int argc, char** argv) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    SVDelegate* delegate = [SVDelegate new];
    delegate.argc = argc;
    delegate.argv = argv;
    [NSApp setDelegate:delegate];
    NSRect rect = NSMakeRect(80, 80, 1100, 740);
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:rect
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                             NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = @"SpaceViewer";
    SVView* view = [[SVView alloc] initWithFrame:rect];
    window.contentView = view;
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
  }
  return 0;
}
