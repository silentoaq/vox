#import "fonts.h"

#import "files.h"
#import "http.h"

namespace {

NSString* const kGoogleCss = @"https://fonts.googleapis.com/css2?family=";
NSString* const kWholeFileAgent = @"curl/8.0";
NSString* const kRegularWeight = @"400";

FontFile failed(NSString* error) { return {nil, error}; }

NSString* firstMatch(NSString* text, NSString* pattern, NSRegularExpressionOptions options) {
  NSRegularExpression* regex = [NSRegularExpression regularExpressionWithPattern:pattern options:options error:nil];
  NSTextCheckingResult* match = [regex firstMatchInString:text options:0 range:NSMakeRange(0, text.length)];
  return match ? [text substringWithRange:[match rangeAtIndex:1]] : nil;
}

FontFile googleFileUrl(const FontRequest& request) {
  NSString* url = [kGoogleCss stringByAppendingString:request.gfont];
  if (![request.weight isEqualToString:kRegularWeight]) url = [url stringByAppendingFormat:@":wght@%@", request.weight];
  NSMutableURLRequest* css = httpRequest(url);
  [css setValue:kWholeFileAgent forHTTPHeaderField:@"User-Agent"];
  HttpResult result = httpFetchNow(css);
  if (result.error) return failed(result.error);
  if (result.status != 200) return failed([NSString stringWithFormat:@"Google Fonts 回 %ld：%@", long(result.status), request.gfont]);
  NSString* text = [[NSString alloc] initWithData:result.body encoding:NSUTF8StringEncoding] ?: @"";
  NSString* file = firstMatch(text, @"url\\((https://[^)]+\\.(?:ttf|otf))\\)", 0);
  if (!file) return failed([@"Google Fonts 沒給完整字檔：" stringByAppendingString:request.gfont]);
  return {file, nil};
}

NSString* extensionOf(NSString* url) {
  NSString* extension = firstMatch(url, @"\\.(ttf|otf|ttc)(?:$|[?#])", NSRegularExpressionCaseInsensitive);
  return extension ? [@"." stringByAppendingString:extension] : @".ttf";
}

}

FontFile fetchFontFile(const FontRequest& request) {
  NSString* url = request.url;
  if (request.gfont.length) {
    FontFile resolved = googleFileUrl(request);
    if (resolved.error) return resolved;
    url = resolved.path;
  }
  NSFileManager* files = NSFileManager.defaultManager;
  NSString* dir = appDataPath(@"fonts");
  [files createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  NSString* base = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@", request.id, request.weight]];
  NSString* path = [base stringByAppendingString:extensionOf(request.zipPath.length ? request.zipPath : url)];
  if ([files fileExistsAtPath:path]) return {path, nil};

  HttpResult file = httpFetchNow(httpRequest(url));
  if (file.error) return failed(file.error);
  if (file.status != 200 || !file.body.length) return failed([NSString stringWithFormat:@"字檔下載失敗（HTTP %ld）：%@", long(file.status), url]);
  NSString* partial = [path stringByAppendingString:@".part"];
  if (!request.zipPath.length) {
    [file.body writeToFile:partial atomically:NO];
  } else {
    NSString* work = [base stringByAppendingString:@".unzip"];
    [files createDirectoryAtPath:work withIntermediateDirectories:YES attributes:nil error:nil];
    NSString* archive = [work stringByAppendingPathComponent:@"font.zip"];
    [file.body writeToFile:archive atomically:NO];
    NSString* error = unzip(archive, work, request.zipPath);
    NSString* extracted = [work stringByAppendingPathComponent:request.zipPath];
    [files removeItemAtPath:partial error:nil];
    if (!error && ![files moveItemAtPath:extracted toPath:partial error:nil]) error = [@"壓縮檔裡沒有 " stringByAppendingString:request.zipPath];
    [files removeItemAtPath:work error:nil];
    if (error) return failed(error);
  }
  NSError* error = nil;
  [files removeItemAtPath:path error:nil];
  if (![files moveItemAtPath:partial toPath:path error:&error]) return failed([@"字檔存不起來：" stringByAppendingString:error.localizedDescription]);
  return {path, nil};
}
