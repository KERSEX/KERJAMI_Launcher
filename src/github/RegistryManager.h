#pragma once

#include "github/RegistryParser.h"
#include "models/Project.h"

#include <QList>
#include <QObject>
#include <QString>

namespace launcher {

/// Holds the list of approved projects.
///
/// The list is part of the program: resources/registry/projects.json is compiled
/// into the executable. Adding a project means a new release of the launcher,
/// which the launcher's self-update delivers. There is no download of the list,
/// no cache and no refresh (docs/03-entscheidungen.md, D-43). The file is still
/// read like untrusted input, with every check of RegistryParser, so that a
/// mistake in it costs one entry and not the whole list.
class RegistryManager : public QObject {
    Q_OBJECT

public:
    /// Where the built-in list lies inside the program.
    static QString builtInResource();

    explicit RegistryManager(QObject* parent = nullptr);

    /// Reads the list built into the program. Call once at start. False if the list
    /// is missing or unusable; there are then no projects (a build with such a file
    /// does not pass the tests).
    bool loadBuiltIn();

    /// Reads a registry document. A document that cannot be used leaves the list as it was.
    bool load(const QByteArray& json);

    /// Enabled, valid projects in the order of the file.
    const QList<Project>& projects() const { return m_projects; }
    QString registryName() const { return m_registryName; }

    /// Entries of the document that were rejected, each with the reason.
    const QList<RegistryIssue>& issues() const { return m_issues; }

    /// Why the last load failed; empty if it worked.
    QString error() const { return m_error; }

signals:
    /// The list changed.
    void changed();

private:
    QList<Project> m_projects;
    QList<RegistryIssue> m_issues;
    QString m_registryName;
    QString m_error;
};

} // namespace launcher
