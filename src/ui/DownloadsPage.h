#pragma once

#include "ui/Page.h"

namespace launcher::ui {

/// Shows running, finished and failed downloads.
class DownloadsPage : public Page {
    Q_OBJECT

public:
    explicit DownloadsPage(QWidget* parent = nullptr);
};

} // namespace launcher::ui
