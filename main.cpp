#include <windows.h>

#include "ui/ui.hpp"
#include "patch/patch.hpp"

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int)
{
	if (!ui_create(inst))
		return 1;

	patch_start_worker();
	ui_run_message_loop();
	patch_shutdown();
	return 0;
}
