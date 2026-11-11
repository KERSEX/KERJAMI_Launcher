#include "app/Application.h"
#include "app/SelfTest.h"
#include "updater/LauncherUpdater.h"

int main(int argc, char* argv[])
{
    // The launcher also replaces its own files after an update: a copy of this
    // program started with --apply-update does that, without a window (D-40).
    if (launcher::LauncherUpdater::isRequested(argc, argv))
        return launcher::LauncherUpdater::runProgram(argc, argv);

    // The packaging scripts check a finished package from the outside with these two modes (D-46).
    if (launcher::SelfTest::isRequested(argc, argv))
        return launcher::SelfTest::runProgram(argc, argv);

    launcher::Application application(argc, argv);
    return application.run();
}
