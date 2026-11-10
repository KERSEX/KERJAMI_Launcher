#include "ui/DownloadsPage.h"

#include "core/Format.h"
#include "downloads/DownloadManager.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

constexpr int kProgressSteps = 1000;

QString problemText(const Download& download)
{
    switch (download.problem) {
    case DownloadProblem::None:
        break;
    case DownloadProblem::InvalidRequest:
        return DownloadsPage::tr("Die Anfrage ist ungültig.");
    case DownloadProblem::InsufficientSpace:
        return DownloadsPage::tr("Nicht genug freier Speicherplatz.");
    case DownloadProblem::StorageError:
        return DownloadsPage::tr("Die Datei konnte nicht gespeichert werden.");
    case DownloadProblem::Unreachable:
        return DownloadsPage::tr("Keine Verbindung.");
    case DownloadProblem::RateLimited:
        return DownloadsPage::tr("GitHub nimmt vorübergehend keine weiteren Anfragen an.");
    case DownloadProblem::Http:
        if (download.httpStatus == 404)
            return DownloadsPage::tr("Die Datei wurde nicht gefunden (HTTP 404).");
        return DownloadsPage::tr("Der Server antwortete mit HTTP %1.").arg(download.httpStatus);
    case DownloadProblem::SizeMismatch:
        return DownloadsPage::tr("Die Datei hat nicht die erwartete Größe.");
    case DownloadProblem::TooLarge:
        return DownloadsPage::tr("Die Datei ist größer als angekündigt.");
    }
    return {};
}

QString statusText(const Download& download)
{
    switch (download.state) {
    case DownloadState::Queued:
        return DownloadsPage::tr("In der Warteschlange");
    case DownloadState::Running: {
        QString text = DownloadsPage::tr("%1 von %2")
                           .arg(format::bytes(download.bytesReceived), format::bytes(download.bytesTotal));
        if (download.bytesPerSecond > 0)
            text += u" · "_s + format::speed(download.bytesPerSecond);
        if (download.remainingSeconds() >= 0)
            text +=
                u" · "_s + DownloadsPage::tr("noch %1").arg(format::duration(download.remainingSeconds()));
        return text;
    }
    case DownloadState::Finished:
        return DownloadsPage::tr("Abgeschlossen · %1 · %2")
            .arg(format::bytes(download.bytesReceived),
                QLocale().toString(download.finishedAt.toLocalTime(), QLocale::ShortFormat));
    case DownloadState::Failed:
        return DownloadsPage::tr("Fehlgeschlagen: %1").arg(problemText(download));
    case DownloadState::Cancelled:
        return DownloadsPage::tr("Abgebrochen");
    }
    return {};
}

} // namespace

/// One download: title, status line, progress bar and, while it is active, a cancel button.
class DownloadRow : public Card {
public:
    DownloadRow(DownloadManager& manager, const Download& download, QWidget* parent)
        : Card(parent)
    {
        setObjectName("DownloadRow");
        setProperty("downloadId", static_cast<qulonglong>(download.id));
        body()->setSpacing(8);

        auto* header = new QHBoxLayout;
        header->setSpacing(12);
        auto* texts = new QVBoxLayout;
        texts->setSpacing(2);
        m_title = createLabel(download.request.title, "title", this);
        m_title->setObjectName("DownloadTitle");
        texts->addWidget(m_title);
        m_status = createLabel(QString(), "secondary", this);
        m_status->setObjectName("DownloadStatus");
        m_status->setWordWrap(true);
        texts->addWidget(m_status);
        header->addLayout(texts, 1);

        if (download.isActive()) {
            auto* cancel = new QPushButton(tr("Abbrechen"), this);
            cancel->setObjectName("DownloadCancel");
            cancel->setCursor(Qt::PointingHandCursor);
            const DownloadId id = download.id;
            connect(cancel, &QPushButton::clicked, this, [&manager, id] { manager.cancel(id); });
            header->addWidget(cancel, 0, Qt::AlignVCenter);
        }
        body()->addLayout(header);

        if (download.isActive()) {
            m_bar = new QProgressBar(this);
            m_bar->setObjectName("DownloadProgress");
            m_bar->setRange(0, kProgressSteps);
            m_bar->setTextVisible(false);
            body()->addWidget(m_bar);
        }
        update(download);
    }

    void update(const Download& download)
    {
        m_status->setText(statusText(download));
        if (m_bar) {
            const qint64 total = download.bytesTotal;
            m_bar->setValue(
                total > 0 ? static_cast<int>(download.bytesReceived * kProgressSteps / total) : 0);
        }
    }

private:
    QLabel* m_title = nullptr;
    QLabel* m_status = nullptr;
    QProgressBar* m_bar = nullptr;
};

DownloadsPage::DownloadsPage(DownloadManager& downloads, QWidget* parent)
    : Page(tr("Downloads"), tr("Laufende und abgeschlossene Downloads."), parent)
    , m_downloads(downloads)
{
    auto* empty = new Card(this);
    empty->setObjectName("DownloadsEmpty");
    empty->body()->addWidget(new EmptyState(Glyph::Download, tr("Keine Downloads"),
        tr("Sobald du ein Projekt installierst oder aktualisierst, siehst du den Fortschritt hier."), empty));
    m_emptyCard = empty;
    contentLayout()->addWidget(m_emptyCard);

    m_activeSection = new QWidget(this);
    auto* activeOuter = new QVBoxLayout(m_activeSection);
    activeOuter->setContentsMargins(0, 0, 0, 0);
    activeOuter->setSpacing(10);
    activeOuter->addWidget(createLabel(tr("Aktiv"), "section", m_activeSection));
    m_activeLayout = new QVBoxLayout;
    m_activeLayout->setSpacing(12);
    activeOuter->addLayout(m_activeLayout);
    contentLayout()->addWidget(m_activeSection);

    m_historySection = new QWidget(this);
    auto* historyOuter = new QVBoxLayout(m_historySection);
    historyOuter->setContentsMargins(0, 0, 0, 0);
    historyOuter->setSpacing(10);
    auto* historyHeader = new QHBoxLayout;
    historyHeader->addWidget(createLabel(tr("Verlauf"), "section", m_historySection));
    historyHeader->addStretch(1);
    m_clearButton = new QPushButton(tr("Verlauf leeren"), m_historySection);
    m_clearButton->setObjectName("DownloadsClear");
    m_clearButton->setCursor(Qt::PointingHandCursor);
    historyHeader->addWidget(m_clearButton);
    historyOuter->addLayout(historyHeader);
    m_historyLayout = new QVBoxLayout;
    m_historyLayout->setSpacing(12);
    historyOuter->addLayout(m_historyLayout);
    contentLayout()->addWidget(m_historySection);
    contentLayout()->addStretch(1);

    connect(m_clearButton, &QPushButton::clicked, &m_downloads, &DownloadManager::clearFinished);
    connect(&m_downloads, &DownloadManager::added, this, &DownloadsPage::rebuild);
    connect(&m_downloads, &DownloadManager::stateChanged, this, &DownloadsPage::rebuild);
    connect(&m_downloads, &DownloadManager::removed, this, &DownloadsPage::rebuild);
    connect(&m_downloads, &DownloadManager::progressChanged, this, &DownloadsPage::updateProgress);

    rebuild();
}

void DownloadsPage::updateProgress(quint64 id)
{
    DownloadRow* row = m_rows.value(id, nullptr);
    const std::optional<Download> download = m_downloads.download(id);
    if (row && download)
        row->update(*download);
}

void DownloadsPage::rebuild()
{
    const auto clear = [](QVBoxLayout* layout) {
        while (QLayoutItem* item = layout->takeAt(0)) {
            if (QWidget* widget = item->widget()) {
                // Detached at once so that nothing finds the old rows while they wait to be deleted.
                widget->setParent(nullptr);
                widget->deleteLater();
            }
            delete item;
        }
    };
    clear(m_activeLayout);
    clear(m_historyLayout);
    m_rows.clear();

    const QList<Download> downloads = m_downloads.downloads();
    int active = 0;
    int history = 0;

    // Active downloads in the order they will run, the history newest first.
    for (const Download& download : downloads) {
        if (!download.isActive())
            continue;
        auto* row = new DownloadRow(m_downloads, download, m_activeSection);
        m_activeLayout->addWidget(row);
        m_rows.insert(download.id, row);
        ++active;
    }
    for (auto it = downloads.crbegin(); it != downloads.crend(); ++it) {
        if (it->isActive())
            continue;
        auto* row = new DownloadRow(m_downloads, *it, m_historySection);
        m_historyLayout->addWidget(row);
        m_rows.insert(it->id, row);
        ++history;
    }

    m_activeSection->setVisible(active > 0);
    m_historySection->setVisible(history > 0);
    m_emptyCard->setVisible(active == 0 && history == 0);
}

} // namespace launcher::ui
