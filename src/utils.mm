#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "utils.hpp"

std::set<std::string> askPaths() {
    std::set<std::string> chosen_paths;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = YES;
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"py"]];
    if ([panel runModal] == NSModalResponseOK) {
        for (NSURL *url in [panel URLs]) {
            chosen_paths.insert([[url path] UTF8String]);
        }
    }
    return chosen_paths;
}

bool isDarkMode() {
    NSAppearance *appearance = NSApp.effectiveAppearance;
    NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:@[
        NSAppearanceNameAqua,
        NSAppearanceNameDarkAqua
    ]];
    return [match isEqualToString:NSAppearanceNameDarkAqua];
}
