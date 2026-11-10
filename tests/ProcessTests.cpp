// Process management: starting a project, knowing whether it runs, and what
// the launcher can and cannot see. The tests start a real program (SleepyApp)
// from a project's folder, as the launcher starts a project.

#include "SleepyProject.h"
#include "process/ProcessManager.h"

#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <thread>
#include <vector>

using namespace Qt::StringLiterals;
using launcher::InstalledProjects;
using launcher::LibraryLayout;
using launcher::ProcessExit;
using launcher::ProcessManager;
using launcher::StartError;

class ProcessTests : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<LibraryLayout> m_layout;
    std::unique_ptr<InstalledProjects> m_installed;
    std::unique_ptr<ProcessManager> m_processes;

    QString current() const { return m_layout->currentDir(u"Tool"_s); }

    bool installTool() { return sleepy::install(*m_layout, *m_installed, u"tool"_s, u"Tool"_s, u"1.0.0"_s); }

    /// Looks up until the condition holds (the programs need a moment to start and to end).
    template <typename Condition> bool refreshUntil(Condition condition, int timeoutMs = 15000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            m_processes->refresh();
            if (condition())
                return true;
            QTest::qWait(100);
        }
        return false;
    }

private slots:
    void initTestCase() { QVERIFY2(QFileInfo::exists(sleepy::appPath()), "SleepyApp.exe is not built"); }

    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        QVERIFY(QDir().mkpath(m_root->filePath(u"library"_s)));
        QVERIFY(QDir().mkpath(m_root->filePath(u"temp"_s)));
        m_layout =
            std::make_unique<LibraryLayout>(m_root->filePath(u"library"_s), m_root->filePath(u"temp"_s));
        m_installed = std::make_unique<InstalledProjects>(m_root->filePath(u"launcher/launcher.json"_s));
        m_installed->load();
        m_processes = std::make_unique<ProcessManager>(*m_layout, *m_installed);
        m_processes->setPolling(false); // the tests decide when to look
    }

    void cleanup()
    {
        // No program of a test may outlive it.
        if (m_layout)
            sleepy::stop(current());
        QTest::qWait(300);
        m_processes.reset();
        m_installed.reset();
        m_layout.reset();
        m_root.reset();
    }

    // --- exit codes -------------------------------------------------------------

    void exitCodes()
    {
        ProcessExit clean {0, 3600};
        QVERIFY(!clean.crashed());
        QVERIFY(!clean.unexpected());

        ProcessExit longRunError {1, 3600}; // a long run that ends with an error code is common
        QVERIFY(!longRunError.crashed());
        QVERIFY(!longRunError.unexpected());

        ProcessExit quickError {1, 2};
        QVERIFY(quickError.unexpected());

        ProcessExit crash {static_cast<int>(0xC0000005u), 3600}; // access violation
        QVERIFY(crash.crashed());
        QVERIFY(crash.unexpected());

        ProcessExit quickClean {0, 1}; // starting and closing quickly is fine
        QVERIFY(!quickClean.unexpected());
    }

    // --- starting ----------------------------------------------------------------

    void startsAProjectAndSeesItEnd()
    {
        QVERIFY(installTool());
        QSignalSpy started(m_processes.get(), &ProcessManager::started);
        QSignalSpy running(m_processes.get(), &ProcessManager::runningChanged);
        QSignalSpy exited(m_processes.get(), &ProcessManager::exited);

        QVERIFY(!m_processes->isRunning(u"tool"_s));
        const auto result = m_processes->start(u"tool"_s);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QVERIFY(result.processId > 0);
        QCOMPARE(started.size(), 1);
        QVERIFY(m_processes->isRunning(u"tool"_s));
        QVERIFY(m_processes->isRunningNow(u"tool"_s));
        QCOMPARE(running.size(), 1);
        QCOMPARE(running.first().at(1).toBool(), true);
        QVERIFY(m_installed->get(u"tool"_s)->lastStartedAt.isValid());

        // Still running after a look-up.
        m_processes->refresh();
        QVERIFY(m_processes->isRunning(u"tool"_s));
        QCOMPARE(exited.size(), 0);

        sleepy::writeControlFile(current(), u"exitcode"_s, "0");
        sleepy::stop(current());
        QVERIFY(refreshUntil([&] { return !m_processes->isRunning(u"tool"_s); }));

        QCOMPARE(exited.size(), 1);
        const auto exit = exited.first().at(1).value<ProcessExit>();
        QCOMPARE(exit.exitCode, 0);
        QVERIFY(!exit.unexpected());
        QCOMPARE(running.size(), 2);
        QCOMPARE(running.last().at(1).toBool(), false);
        QVERIFY(m_processes->lastExit(u"tool"_s).has_value());
        QVERIFY(!m_processes->isRunningNow(u"tool"_s));

        m_processes->clearLastExit(u"tool"_s);
        QVERIFY(!m_processes->lastExit(u"tool"_s).has_value());
    }

    void startingTwiceIsRefused()
    {
        QVERIFY(installTool());
        QVERIFY(m_processes->start(u"tool"_s).ok());

        const auto second = m_processes->start(u"tool"_s);
        QCOMPARE(second.error, StartError::AlreadyRunning);

        // After the program ended, a start works again.
        sleepy::stop(current());
        QVERIFY(refreshUntil([&] { return !m_processes->isRunning(u"tool"_s); }));
        QFile::remove(current() + u"/stop"_s);
        QVERIFY(m_processes->start(u"tool"_s).ok());
        QVERIFY(m_processes->isRunning(u"tool"_s));
    }

    void reportsAnErrorExitAndACrash()
    {
        QVERIFY(installTool());
        QSignalSpy exited(m_processes.get(), &ProcessManager::exited);

        sleepy::writeControlFile(current(), u"exitcode"_s, "3");
        sleepy::stop(current()); // ends at once
        QVERIFY(m_processes->start(u"tool"_s).ok());
        QVERIFY(refreshUntil([&] { return exited.size() == 1; }));
        auto exit = exited.last().at(1).value<ProcessExit>();
        QCOMPARE(exit.exitCode, 3);
        QVERIFY(exit.unexpected()); // an error code right after the start
        QVERIFY(!exit.crashed());

        QFile::remove(current() + u"/stop"_s);
        sleepy::writeControlFile(current(), u"exitcode"_s, "3221225477"); // 0xC0000005
        sleepy::stop(current());
        QVERIFY(refreshUntil([&] { return !m_processes->isRunningNow(u"tool"_s); }));
        QVERIFY(m_processes->start(u"tool"_s).ok());
        QVERIFY(refreshUntil([&] { return exited.size() == 2; }));
        exit = exited.last().at(1).value<ProcessExit>();
        QVERIFY(exit.crashed());
        QCOMPARE(static_cast<quint32>(exit.exitCode), 0xC0000005u);
    }

    void refusesWhatItShouldNotStart()
    {
        QCOMPARE(m_processes->start(u"tool"_s).error, StartError::NotInstalled);

        QVERIFY(installTool());
        QVERIFY(QFile::remove(current() + u"/Tool.exe"_s));
        QCOMPARE(m_processes->start(u"tool"_s).error, StartError::FilesMissing);
        QVERIFY(!m_processes->isRunning(u"tool"_s));
    }

    // --- seeing what it did not start ----------------------------------------------

    void seesAProjectStartedElsewhere()
    {
        // From Explorer, a shortcut, or an earlier run of the launcher: nobody told the manager.
        QVERIFY(installTool());
        QVERIFY(!m_processes->isRunningNow(u"tool"_s));

        QVERIFY(QProcess::startDetached(current() + u"/Tool.exe"_s, {}, current()));
        QSignalSpy started(m_processes.get(), &ProcessManager::started);
        QSignalSpy running(m_processes.get(), &ProcessManager::runningChanged);
        QVERIFY(refreshUntil([&] { return m_processes->isRunning(u"tool"_s); }));
        QVERIFY(m_processes->isRunningNow(u"tool"_s));
        QCOMPARE(started.size(), 0); // not started by the launcher
        QVERIFY(running.size() >= 1);

        // Starting it again is refused, whoever started the first one.
        QCOMPARE(m_processes->start(u"tool"_s).error, StartError::AlreadyRunning);

        sleepy::stop(current());
        QVERIFY(refreshUntil([&] { return !m_processes->isRunning(u"tool"_s); }));
    }

    void anotherFolderIsNotThisProject()
    {
        // The same program, but in a folder that does not belong to the project.
        QVERIFY(installTool());
        const QString elsewhere = m_root->filePath(u"somewhere"_s);
        QVERIFY(QDir().mkpath(elsewhere));
        QVERIFY(QFile::copy(sleepy::appPath(), elsewhere + u"/Tool.exe"_s));
        QVERIFY(QProcess::startDetached(elsewhere + u"/Tool.exe"_s, {}, elsewhere));
        QTest::qWait(500);

        QVERIFY(!m_processes->isRunningNow(u"tool"_s));
        m_processes->refresh();
        QVERIFY(!m_processes->isRunning(u"tool"_s));

        sleepy::stop(elsewhere);
        QTest::qWait(300);
    }

    void aSiblingFolderWithTheSamePrefixIsNotThisProject()
    {
        // "Tool/current" must not match a program in "Tool/current-old".
        QVERIFY(installTool());
        const QString sibling = m_layout->projectDir(u"Tool"_s) + u"/current-old"_s;
        QVERIFY(QDir().mkpath(sibling));
        QVERIFY(QFile::copy(sleepy::appPath(), sibling + u"/Tool.exe"_s));
        QVERIFY(QProcess::startDetached(sibling + u"/Tool.exe"_s, {}, sibling));
        QTest::qWait(500);
        QVERIFY(!m_processes->isRunningNow(u"tool"_s));

        sleepy::stop(sibling);
        QTest::qWait(300);
    }

    void followsWhatTheProgramStartsEvenOutsideItsFolder()
    {
        // The program starts a helper from another folder and ends. The helper
        // is not in the project's folder, so only the job object knows it belongs to the project.
        QVERIFY(installTool());
        const QString helperFolder = m_root->filePath(u"helper"_s);
        QVERIFY(
            sleepy::writeControlFile(current(), u"spawn"_s, QDir::toNativeSeparators(helperFolder).toUtf8()));
        QVERIFY(sleepy::writeControlFile(current(), u"mainexits"_s));

        QSignalSpy exited(m_processes.get(), &ProcessManager::exited);
        QVERIFY(m_processes->start(u"tool"_s).ok());
        QVERIFY(refreshUntil([&] { return exited.size() == 1; }));
        QCOMPARE(exited.first().at(1).value<ProcessExit>().exitCode, 0);

        // The program that was started is gone, the helper is not: the project still runs.
        m_processes->refresh();
        QVERIFY(m_processes->isRunning(u"tool"_s));
        QVERIFY(m_processes->isRunningNow(u"tool"_s));
        QVERIFY(QFileInfo::exists(helperFolder + u"/Child.exe"_s));

        sleepy::stop(current());
        QVERIFY(refreshUntil([&] { return !m_processes->isRunning(u"tool"_s); }));
        QVERIFY(!m_processes->isRunningNow(u"tool"_s));
    }

    // --- misc ----------------------------------------------------------------------

    void unknownProjectsDoNotRun()
    {
        QVERIFY(!m_processes->isRunningNow(u"nothing"_s));
        QVERIFY(!m_processes->isRunning(u"nothing"_s));
        QVERIFY(m_processes->runningProjects().isEmpty());
        m_processes->refresh(); // with nothing installed, nothing is looked up and nothing fails
    }

    void canBeAskedFromOtherThreads()
    {
        // The installer asks from its worker thread while the main thread starts and looks up.
        QVERIFY(installTool());
        std::atomic<bool> stop = false;
        std::atomic<int> asked = 0;
        std::vector<std::thread> threads;
        for (int i = 0; i < 3; ++i) {
            threads.emplace_back([&] {
                while (!stop) {
                    m_processes->isRunningNow(u"tool"_s);
                    ++asked;
                }
            });
        }
        QVERIFY(m_processes->start(u"tool"_s).ok());
        for (int i = 0; i < 5; ++i) {
            m_processes->refresh();
            QTest::qWait(50);
        }
        sleepy::stop(current());
        QVERIFY(refreshUntil([&] { return !m_processes->isRunning(u"tool"_s); }));
        stop = true;
        for (std::thread& thread : threads)
            thread.join();
        QVERIFY(asked > 0);
    }
};

QTEST_GUILESS_MAIN(ProcessTests)
#include "ProcessTests.moc"
