#import <Foundation/Foundation.h>

#include "scan_support.hpp"

#include <cstdint>

namespace spaceviewer::apple {

bool volumeCapacity(const std::string& path, uint64_t& total, uint64_t& available) {
  @autoreleasepool {
    NSString* pathString = [NSString stringWithUTF8String:path.c_str()];
    if (!pathString) return false;
    NSURL* url = [NSURL fileURLWithPath:pathString isDirectory:YES];
    NSError* error = nil;
    NSDictionary<NSURLResourceKey, id>* values =
        [url resourceValuesForKeys:@[NSURLVolumeTotalCapacityKey,
                                     NSURLVolumeAvailableCapacityForImportantUsageKey,
                                     NSURLVolumeAvailableCapacityKey]
                            error:&error];
    NSNumber* totalValue = values[NSURLVolumeTotalCapacityKey];
    NSNumber* availableValue = values[NSURLVolumeAvailableCapacityForImportantUsageKey];
    if (!availableValue) availableValue = values[NSURLVolumeAvailableCapacityKey];
    if (!totalValue || !availableValue || totalValue.longLongValue < 0 || availableValue.longLongValue < 0) return false;
    total = static_cast<uint64_t>(totalValue.longLongValue);
    available = static_cast<uint64_t>(availableValue.longLongValue);
    return true;
  }
}

}  // namespace spaceviewer::apple
