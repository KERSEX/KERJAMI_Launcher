#include "app/Branding.h"

#include "BrandingConfig.h"

namespace launcher::branding {

QString productName()
{
    return QStringLiteral(LAUNCHER_BRAND_PRODUCT_NAME);
}

QString displayName()
{
    return QStringLiteral(LAUNCHER_BRAND_DISPLAY_NAME);
}

QString version()
{
    return QStringLiteral(LAUNCHER_VERSION_STRING);
}

QString executableFileName()
{
    return QStringLiteral(LAUNCHER_BRAND_EXECUTABLE_NAME ".exe");
}

QString gitHubOwner()
{
    return QStringLiteral(LAUNCHER_BRAND_GITHUB_OWNER);
}

QString launcherRepository()
{
    return QStringLiteral(LAUNCHER_BRAND_LAUNCHER_REPOSITORY);
}

QString updateAssetPattern()
{
    return QStringLiteral(LAUNCHER_BRAND_UPDATE_ASSET_PATTERN);
}

QUrl registryUrl()
{
    return QUrl(QStringLiteral(LAUNCHER_BRAND_REGISTRY_URL));
}

} // namespace launcher::branding
