#include "../../IO.h"

#import <Cocoa/Cocoa.h>

namespace IO
{
	namespace
	{
		NSAlertStyle toNSAlertStyle(MessageBoxIcon icon)
		{
			switch (icon)
			{
			case MessageBoxIcon::Warning:
			case MessageBoxIcon::Error:
				return NSAlertStyleCritical;
			case MessageBoxIcon::Information:
			case MessageBoxIcon::Question:
			default:
				return NSAlertStyleInformational;
			}
		}

		NSString* nsString(const std::string& s)
		{
			return [NSString stringWithUTF8String:s.c_str()];
		}
	}

	MessageBoxResult messageBox(std::string title, std::string message, MessageBoxButtons buttons, MessageBoxIcon icon, void* /*parentWindow*/)
	{
		@autoreleasepool
		{
			NSAlert* alert = [[NSAlert alloc] init];
			alert.alertStyle = toNSAlertStyle(icon);
			alert.messageText = nsString(title);
			alert.informativeText = nsString(message);

			switch (buttons)
			{
			case MessageBoxButtons::Ok:
				[alert addButtonWithTitle:@"OK"];
				break;
			case MessageBoxButtons::OkCancel:
				[alert addButtonWithTitle:@"OK"];
				[alert addButtonWithTitle:@"Cancel"];
				break;
			case MessageBoxButtons::YesNo:
				[alert addButtonWithTitle:@"Yes"];
				[alert addButtonWithTitle:@"No"];
				break;
			case MessageBoxButtons::YesNoCancel:
				[alert addButtonWithTitle:@"Yes"];
				[alert addButtonWithTitle:@"No"];
				[alert addButtonWithTitle:@"Cancel"];
				break;
			}

			NSModalResponse response = [alert runModal];
			long idx = response - NSAlertFirstButtonReturn;

			switch (buttons)
			{
			case MessageBoxButtons::Ok:
				return MessageBoxResult::Ok;
			case MessageBoxButtons::OkCancel:
				return idx == 0 ? MessageBoxResult::Ok : MessageBoxResult::Cancel;
			case MessageBoxButtons::YesNo:
				return idx == 0 ? MessageBoxResult::Yes : MessageBoxResult::No;
			case MessageBoxButtons::YesNoCancel:
				if (idx == 0) return MessageBoxResult::Yes;
				if (idx == 1) return MessageBoxResult::No;
				return MessageBoxResult::Cancel;
			}

			return MessageBoxResult::None;
		}
	}
}
