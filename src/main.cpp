#include "app/Application.h"
#include "updater/LauncherUpdater.h"

int main(int argc, char* argv[])
{
    // The launcher also replaces its own files after an update: a copy of this
    // program started with --apply-update does that, without a window (D-40).
    if (launcher::LauncherUpdater::isRequested(argc, argv))
        return launcher::LauncherUpdater::runProgram(argc, argv);

    launcher::Application application(argc, argv);
    return application.run();
}
