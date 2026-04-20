#include "../../File.h"
#include "../../IO.h"

namespace IO
{
	FileDialogResult FileDialog::showFileDialog(DialogType /*type*/, DialogSelectType /*selectType*/)
	{
		// The Linux build ships only the offline renderer CLI. File pickers are
		// never shown interactively; keep a stub so the symbol resolves even if
		// dead code references it.
		return FileDialogResult::Cancel;
	}
}
