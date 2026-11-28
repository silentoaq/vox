#import "files.h"

#include "text.h"

#import "common.h"

NSString* resourcesDir() { return NSBundle.mainBundle.resourcePath; }

NSString* appDataDir() {
  NSURL* base = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
  NSString* dir = [base.path stringByAppendingPathComponent:ns(std::wstring(APP_NAME))];
  [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  return dir;
}

NSString* appDataPath(NSString* name) { return [appDataDir() stringByAppendingPathComponent:name]; }

NSString* readTextFile(NSString* path) { return [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil]; }

void writeDataFile(NSString* path, NSData* data) {
  NSError* error = nil;
  if (![data writeToFile:path options:NSDataWritingAtomic error:&error]) throw HostError(error.localizedDescription);
}

void writeTextFile(NSString* path, NSString* text) { writeDataFile(path, [text dataUsingEncoding:NSUTF8StringEncoding]); }

int runTool(NSString* tool, NSArray<NSString*>* arguments) {
  NSTask* task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:tool];
  task.arguments = arguments;
  task.standardOutput = NSFileHandle.fileHandleWithNullDevice;
  task.standardError = NSFileHandle.fileHandleWithNullDevice;
  NSError* error = nil;
  if (![task launchAndReturnError:&error]) return -1;
  [task waitUntilExit];
  return task.terminationStatus;
}

NSString* unzip(NSString* archive, NSString* dir, NSString* member) {
  NSMutableArray* arguments = [@[ @"-xf", archive, @"-C", dir ] mutableCopy];
  if (member.length) [arguments addObject:member];
  const int code = runTool(@"/usr/bin/tar", arguments);
  if (code < 0) return ns(tr(Text::UnzipStart, {L"-1"}));
  return code == 0 ? nil : ns(tr(Text::UnzipFailed, {std::to_wstring(code)}));
}
