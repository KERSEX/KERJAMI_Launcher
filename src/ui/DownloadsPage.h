#pragma once

#include "models/Download.h"
#include "ui/Page.h"

#include <QHash>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace launcher {
class DownloadManager;
}

namespace launcher::ui {

class DownloadRow;

/// Shows running, finished and failed downloads of this session.
///
/// The page only displays what the DownloadManager holds and asks it to
/// cancel or to forget old entries.
class DownloadsPage : public Page {
    Q_OBJECT

public:
    explicit DownloadsPage(DownloadManager& downloads, QWidget* parent = nullptr);

private:
    /// Builds all rows anew: an entry was added, removed or changed its state.
    void rebuild();

    /// Updates the numbers of one running download without rebuilding.
    void updateProgress(quint64 id);

    DownloadManager& m_downloads;
    QHash<DownloadId, DownloadRow*> m_rows;

    QWidget* m_emptyCard = nullptr;
    QWidget* m_activeSection = nullptr;
    QWidget* m_historySection = nullptr;
    QVBoxLayout* m_activeLayout = nullptr;
    QVBoxLayout* m_historyLayout = nullptr;
    QPushButton* m_clearButton = nullptr;
};

} // namespace launcher::ui
