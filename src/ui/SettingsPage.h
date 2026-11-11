#pragma once

#include "installer/Storage.h"
#include "ui/AppContext.h"
#include "ui/Dialogs.h"
#include "ui/Page.h"

#include <QList>

#include <atomic>
#include <functional>

class QLabel;
class QPushButton;
class QThread;

namespace launcher::ui {

/// Preferences of the launcher. Every control writes through to the
/// SettingsManager as soon as it changes; there is no "Apply" button.
class SettingsPage : public Page {
    Q_OBJECT

public:
    explicit SettingsPage(const AppContext& context, QWidget* parent = nullptr);
    ~SettingsPage() override;

protected:
    /// The sizes are measured when the page is shown: they change whenever projects are installed.
    void showEvent(QShowEvent* event) override;

private:
    QWidget* createStorageCard(const PathManager& paths);
    QWidget* createUsageCard();
    QWidget* createUpdatesCard();
    QWidget* createStartupCard();
    QWidget* createAboutCard(const PathManager& paths, LauncherUpdateManager& launcherUpdates);

    // --- storage: what the launcher's folders take, and cleaning up ---
    void measureUsage();
    void showUsage(const Storage::Usage& usage);
    void updateCleanButtons();
    bool cleanUpAllowed() const;
    void cleanUp(Dialogs::StorageCleanup what);

    /// Runs @p work in a thread of its own and calls @p done in this one with its result.
    void runInBackground(std::function<QString()> work, std::function<void(const QString&)> done);

    /// Title and description on the left, an optional control on the right.
    QWidget* createRow(const QString& title, const QString& description, QWidget* control);

    AppContext m_context;
    SettingsManager& m_settings;

    Storage::Usage m_usage;
    bool m_usageKnown = false;
    bool m_cleaning = false;
    QLabel* m_downloadsSize = nullptr;
    QLabel* m_backupsSize = nullptr;
    QLabel* m_usageProblem = nullptr;
    QPushButton* m_cleanDownloads = nullptr;
    QPushButton* m_cleanBackups = nullptr;

    std::atomic<bool> m_stopping {false};
    QList<QThread*> m_threads;
};

} // namespace launcher::ui
