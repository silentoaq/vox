#pragma once
#import <Foundation/Foundation.h>

NSString* resourcesDir();
NSString* appDataDir();
NSString* appDataPath(NSString* name);
NSString* readTextFile(NSString* path);
void writeTextFile(NSString* path, NSString* text);
void writeDataFile(NSString* path, NSData* data);
NSString* unzip(NSString* archive, NSString* dir, NSString* member);
int runTool(NSString* tool, NSArray<NSString*>* arguments);
