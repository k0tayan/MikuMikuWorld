#include "../../File.h"
#include "../../IO.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>

namespace IO
{
	namespace
	{
		// Convert "*.mmws;*.json" -> @[@"mmws", @"json"]. "*.*" returns nil (any type).
		NSArray<NSString*>* filterTypeToUTIs(const std::string& filterType)
		{
			if (filterType == "*.*" || filterType.empty())
				return nil;

			NSMutableArray<NSString*>* arr = [NSMutableArray array];
			size_t i = 0;
			while (i < filterType.size())
			{
				size_t semi = filterType.find(';', i);
				std::string token = filterType.substr(i, semi == std::string::npos ? std::string::npos : semi - i);
				// Strip leading "*." or "."
				if (token.rfind("*.", 0) == 0) token.erase(0, 2);
				else if (!token.empty() && token[0] == '.') token.erase(0, 1);

				if (!token.empty() && token != "*")
				{
					NSString* ext = [NSString stringWithUTF8String:token.c_str()];
					if (![arr containsObject:ext])
						[arr addObject:ext];
				}

				if (semi == std::string::npos) break;
				i = semi + 1;
			}

			return arr.count > 0 ? arr : nil;
		}

		NSArray<NSString*>* collectAllowedTypes(const std::vector<FileDialogFilter>& filters)
		{
			NSMutableArray<NSString*>* all = [NSMutableArray array];
			bool anyUnrestricted = false;
			for (const auto& f : filters)
			{
				NSArray<NSString*>* types = filterTypeToUTIs(f.filterType);
				if (!types)
				{
					anyUnrestricted = true;
					break;
				}
				for (NSString* t in types)
					if (![all containsObject:t])
						[all addObject:t];
			}
			if (anyUnrestricted || all.count == 0) return nil;
			return all;
		}
	}

	FileDialogResult FileDialog::showFileDialog(DialogType type, DialogSelectType selectType)
	{
		@autoreleasepool
		{
			NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
			NSArray<NSString*>* allowed = collectAllowedTypes(filters);

			if (type == DialogType::Save)
			{
				NSSavePanel* panel = [NSSavePanel savePanel];
				panel.title = nsTitle;
				panel.canCreateDirectories = YES;
				if (allowed) panel.allowedFileTypes = allowed;

				if (!inputFilename.empty())
					panel.nameFieldStringValue = [NSString stringWithUTF8String:inputFilename.c_str()];

				if ([panel runModal] != NSModalResponseOK)
					return FileDialogResult::Cancel;

				NSURL* url = panel.URL;
				if (!url) return FileDialogResult::Cancel;
				outputFilename = [url.path UTF8String];
			}
			else
			{
				NSOpenPanel* panel = [NSOpenPanel openPanel];
				panel.title = nsTitle;
				panel.canChooseFiles = (selectType == DialogSelectType::File) ? YES : NO;
				panel.canChooseDirectories = (selectType == DialogSelectType::Folder) ? YES : NO;
				panel.allowsMultipleSelection = NO;
				if (allowed) panel.allowedFileTypes = allowed;

				if ([panel runModal] != NSModalResponseOK)
					return FileDialogResult::Cancel;

				NSURL* url = panel.URLs.firstObject;
				if (!url) return FileDialogResult::Cancel;
				outputFilename = [url.path UTF8String];
			}

			return outputFilename.empty() ? FileDialogResult::Cancel : FileDialogResult::OK;
		}
	}
}
