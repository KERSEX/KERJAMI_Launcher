#pragma once

#include "ui/Page.h"

namespace launcher::ui {

/// Lists the approved projects from the registry.
class LibraryPage : public Page {
    Q_OBJECT

public:
    explicit LibraryPage(QWidget* parent = nullptr);
};

} // namespace launcher::ui
