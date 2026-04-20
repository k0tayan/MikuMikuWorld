#pragma once
#include <string>

namespace MikuMikuWorld::Platform
{
	// Returns the directory to read read-only resources from, with a trailing '/'.
	// macOS: <bundle>/Contents/Resources/
	// Linux: directory next to the executable (or a systemwide share path).
	std::string getResourceDir();

	// Returns a writable per-user data directory, with a trailing '/'.
	// macOS: ~/Library/Application Support/MikuMikuWorld/
	// Linux: $XDG_DATA_HOME/MikuMikuWorld/ or ~/.local/share/MikuMikuWorld/
	// The directory is created on first access if it does not exist.
	std::string getUserDataDir();
}
