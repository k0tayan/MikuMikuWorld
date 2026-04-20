#include "../../IO.h"

#include <cstdio>

namespace IO
{
	MessageBoxResult messageBox(std::string title, std::string message, MessageBoxButtons buttons, MessageBoxIcon /*icon*/, void* /*parentWindow*/)
	{
		std::fprintf(stderr, "[%s] %s\n", title.c_str(), message.c_str());
		switch (buttons)
		{
		case MessageBoxButtons::Ok:
			return MessageBoxResult::Ok;
		case MessageBoxButtons::OkCancel:
			return MessageBoxResult::Cancel;
		case MessageBoxButtons::YesNo:
			return MessageBoxResult::No;
		case MessageBoxButtons::YesNoCancel:
			return MessageBoxResult::Cancel;
		}
		return MessageBoxResult::None;
	}
}
