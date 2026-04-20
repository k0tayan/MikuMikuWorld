#include "../../Utilities.h"

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <string>

namespace MikuMikuWorld
{
	std::string Utilities::getSystemLocale()
	{
		auto fromEnv = [](const char* name) -> std::string {
			const char* v = std::getenv(name);
			return (v && *v) ? std::string(v) : std::string();
		};

		std::string raw = fromEnv("LC_ALL");
		if (raw.empty()) raw = fromEnv("LC_MESSAGES");
		if (raw.empty()) raw = fromEnv("LANG");

		if (raw.empty())
		{
			if (const char* cur = std::setlocale(LC_MESSAGES, nullptr))
				raw = cur;
		}

		if (raw.empty() || raw == "C" || raw == "POSIX")
			return "en";

		// "en_US.UTF-8" / "ja_JP" / "zh_TW.UTF-8" -> language code
		size_t sep = raw.find_first_of("_.@");
		std::string lang = (sep == std::string::npos) ? raw : raw.substr(0, sep);
		if (lang.empty()) return "en";
		return lang;
	}
}
