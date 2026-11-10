// LauncherUpdater.exe: replaces the launcher's program files after the launcher
// has closed. Usage: LauncherUpdater.exe --job <job.json> [--quiet]
// --quiet shows no message boxes (for automated runs).
//
// The launcher writes the job file and starts a copy of this program from
// temp/, then quits. See docs/03-entscheidungen.md, D-40.

#include "LauncherUpdater.h"
#include "core/JsonFile.h"

#include <QCoreApplication>
#include <QStringList>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);

    const QStringList arguments = application.arguments();
    const qsizetype flag = arguments.indexOf(u"--job"_s);
    if (flag < 0 || flag + 1 >= arguments.size())
        return launcher::LauncherUpdater::ExitBadJob;

    const launcher::JsonFile::LoadResult file = launcher::JsonFile::load(arguments.at(flag + 1));
    if (file.status != launcher::JsonFile::Status::Ok)
        return launcher::LauncherUpdater::ExitBadJob;
    const std::optional<launcher::UpdaterJob> job = launcher::UpdaterJob::fromJson(file.object);
    if (!job)
        return launcher::LauncherUpdater::ExitBadJob;

    return launcher::LauncherUpdater::run(*job, !arguments.contains(u"--quiet"_s)).exitCode;
}
