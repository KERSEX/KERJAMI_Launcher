#include "github/RegistryManager.h"

#include "core/Logger.h"

#include <QFile>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

void logIssues(const RegistryParseResult& parsed)
{
    for (const RegistryIssue& issue : parsed.issues) {
        qCWarning(lcRegistry).noquote()
            << "Rejected project entry" << issue.index
            << (issue.projectId.isEmpty() ? QString() : u'(' + issue.projectId + u')') << '-'
            << issue.message;
    }
}

} // namespace

QString RegistryManager::builtInResource()
{
    return u":/registry/projects.json"_s;
}

RegistryManager::RegistryManager(QObject* parent)
    : QObject(parent)
{
}

bool RegistryManager::loadBuiltIn()
{
    QFile file(builtInResource());
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = u"the built-in project list is missing"_s;
        qCWarning(lcRegistry) << "The built-in project list is missing";
        return false;
    }
    return load(file.readAll());
}

bool RegistryManager::load(const QByteArray& json)
{
    const RegistryParseResult parsed = RegistryParser::parse(json);
    if (!parsed.ok()) {
        m_error = parsed.error;
        qCWarning(lcRegistry).noquote() << "The project list is not usable:" << parsed.error;
        return false;
    }

    logIssues(parsed);
    m_projects = parsed.projects;
    m_issues = parsed.issues;
    m_registryName = parsed.registryName;
    m_error.clear();

    qCInfo(lcRegistry) << "Project list loaded:" << m_projects.size() << "projects," << parsed.disabledCount
                       << "disabled," << parsed.issues.size() << "rejected";
    emit changed();
    return true;
}

} // namespace launcher
