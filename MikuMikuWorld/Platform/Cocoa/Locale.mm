#include "../../Utilities.h"

#import <Foundation/Foundation.h>

namespace MikuMikuWorld
{
	std::string Utilities::getSystemLocale()
	{
		@autoreleasepool
		{
			NSString* identifier = [[NSLocale preferredLanguages] firstObject];
			if (!identifier) identifier = [[NSLocale currentLocale] languageCode];
			if (!identifier) return "en";

			// macOS identifiers look like "en-US" or "ja"; we only want the language code.
			NSRange dash = [identifier rangeOfString:@"-"];
			if (dash.location != NSNotFound)
				identifier = [identifier substringToIndex:dash.location];

			return [identifier UTF8String];
		}
	}
}
