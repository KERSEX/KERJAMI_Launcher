#pragma once

#include <QString>
#include <QUrl>

/// Product identity and distribution endpoints.
///
/// All values come from cmake/Branding.cmake. Code must use these accessors
/// instead of spelling out the product name, so the brand can change in one place.
namespace launcher::branding {

/// Short product name, e.g. for sentences in the user interface.
QString productName();

/// Full name shown in the window title.
QString displayName();

/// Semantic version of this build, e.g. "1.0.0".
QString version();

/// File name of the launcher executable including ".exe".
QString executableFileName();

/// File name of the self-update helper including ".exe".
QString updaterFileName();

/// GitHub account that owns the launcher repository.
QString gitHubOwner();

/// GitHub repository that publishes the launcher's own releases.
QString launcherRepository();

/// File name pattern of the portable ZIP of a launcher release, "*" for the version.
QString updateAssetPattern();

/// HTTPS address of the official project registry. Not user-configurable.
QUrl registryUrl();

} // namespace launcher::branding
