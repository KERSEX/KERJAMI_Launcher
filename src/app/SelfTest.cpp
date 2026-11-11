#include "app/SelfTest.h"

#include "app/Branding.h"
#include "core/JsonFile.h"
#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "github/RegistryManager.h"
#include "security/HashVerifier.h"
#include "updater/UpdatePackage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslSocket>
#include <QStringList>

#include <cstring>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr int kOk = 0;
constexpr int kChecksFailed = 1;
constexpr int kBadArguments = 2;

struct Check {
    QString name;
    bool ok = false;
    QString detail;
};

QJsonObject toJson(const Check& check)
{
    return {{u"name"_s, check.name}, {u"ok"_s, check.ok}, {u"detail"_s, check.detail}};
}

bool writeResult(const QString& path, const QJsonObject& result)
{
    QString error;
    return JsonFile::saveAtomic(path, result, &error);
}

/// The value after a flag, or an empty string.
QString option(const QStringList& arguments, const QString& flag)
{
    const qsizetype index = arguments.indexOf(flag);
    return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString();
}

// ---- --self-test ----------------------------------------------------------------

Check checkTls()
{
    Check check {u"tls"_s, false, {}};
    const bool supported = QSslSocket::supportsSsl();
    check.ok = supported && !QSslSocket::activeBackend().isEmpty();
    check.detail = u"backend \"%1\", library \"%2\""_s.arg(
        QSslSocket::activeBackend(), supported ? QSslSocket::sslLibraryVersionString() : u"none"_s);
    return check;
}

Check checkHashing()
{
    // The test vector of FIPS 180-2: SHA-256 of "abc".
    static const QByteArray expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const QByteArray actual = QCryptographicHash::hash("abc", QCryptographicHash::Sha256).toHex();
    return {u"sha256"_s, actual == expected && HashVerifier::isValidSha256(QString::fromLatin1(actual)),
        QString::fromLatin1(actual)};
}

Check checkDecompression()
{
    // The zlib inside Qt inflates the entries of every archive the launcher installs.
    QByteArray original;
    for (int i = 0; i < 4000; ++i)
        original += QByteArray::number(i) + ',';
    const QByteArray packed = qCompress(original, 9);
    const bool ok = packed.size() < original.size() && qUncompress(packed) == original;
    return {u"zlib"_s, ok, u"%1 bytes packed to %2"_s.arg(original.size()).arg(packed.size())};
}

Check checkProjectList()
{
    RegistryManager registry;
    const bool loaded = registry.loadBuiltIn();
    const bool clean = registry.issues().isEmpty();
    Check check {u"project-list"_s, loaded && clean, {}};
    check.detail = loaded
        ? u"%1 projects, %2 rejected"_s.arg(registry.projects().size()).arg(registry.issues().size())
        : registry.error();
    return check;
}

Check checkResources()
{
    // The style sheet and the images are compiled into the program.
    const QStringList wanted {u":/styles/app.qss"_s, u":/images/logo.png"_s, u":/images/app-256.png"_s,
        RegistryManager::builtInResource()};
    QStringList missing;
    for (const QString& path : wanted) {
        if (!QFile::exists(path))
            missing.append(path);
    }
    return {u"resources"_s, missing.isEmpty(),
        missing.isEmpty() ? u"%1 resources found"_s.arg(wanted.size())
                          : u"missing: "_s + missing.join(u", "_s)};
}

Check checkPortableFolder(const PathManager& paths)
{
    // As at every start: the folders are created and must be writable; nothing goes elsewhere.
    const PathManager::PrepareResult prepared = paths.prepare();
    if (!prepared.ok())
        return {
            u"portable-folder"_s, false, u"cannot prepare %1"_s.arg(QDir::toNativeSeparators(prepared.path))};

    const QString probe = paths.configDir() + u"/self-test.json"_s;
    QString error;
    const bool wrote = JsonFile::saveAtomic(probe, QJsonObject {{u"probe"_s, true}}, &error);
    const JsonFile::LoadResult read = JsonFile::load(probe);
    const bool readBack = read.status == JsonFile::Status::Ok && read.object.value(u"probe"_s).toBool();
    QFile::remove(probe);
    return {u"portable-folder"_s, wrote && readBack,
        wrote ? u"folders created, a file written and read"_s : error};
}

Check checkSettings(const PathManager& paths)
{
    SettingsManager settings(paths.settingsFile());
    const SettingsManager::LoadResult loaded = settings.load();
    const bool ok = loaded == SettingsManager::LoadResult::Loaded
        || loaded == SettingsManager::LoadResult::CreatedDefaults;
    return {u"settings"_s, ok && QFileInfo::exists(paths.settingsFile()),
        u"settings file %1"_s.arg(ok ? u"readable"_s : u"not usable"_s)};
}

int runSelfTest(const QString& outputPath)
{
    const PathManager paths = PathManager::forApplication();

    QList<Check> checks;
    checks.append(Check {u"program"_s, true,
        u"%1 %2, %3"_s.arg(branding::productName(), branding::version(), branding::executableFileName())});
    checks.append(checkTls());
    checks.append(checkHashing());
    checks.append(checkDecompression());
    checks.append(checkResources());
    checks.append(checkProjectList());
    checks.append(checkPortableFolder(paths));
    checks.append(checkSettings(paths));

    bool allOk = true;
    QJsonArray list;
    for (const Check& check : std::as_const(checks)) {
        allOk = allOk && check.ok;
        list.append(toJson(check));
    }
    const QJsonObject result {{u"ok"_s, allOk}, {u"product"_s, branding::productName()},
        {u"version"_s, branding::version()}, {u"executable"_s, branding::executableFileName()},
        {u"checks"_s, list}};
    if (!writeResult(outputPath, result))
        return kBadArguments;
    return allOk ? kOk : kChecksFailed;
}

// ---- --check-package ------------------------------------------------------------

QString errorName(PackageError error)
{
    switch (error) {
    case PackageError::None:
        return u"none"_s;
    case PackageError::NoTrustedHash:
        return u"no-trusted-hash"_s;
    case PackageError::HashMismatch:
        return u"hash-mismatch"_s;
    case PackageError::ArchiveUnreadable:
        return u"archive-unreadable"_s;
    case PackageError::ArchiveRejected:
        return u"archive-rejected"_s;
    case PackageError::PathTooLong:
        return u"path-too-long"_s;
    case PackageError::InsufficientSpace:
        return u"insufficient-space"_s;
    case PackageError::ExtractionFailed:
        return u"extraction-failed"_s;
    case PackageError::ManifestMissing:
        return u"manifest-missing"_s;
    case PackageError::ManifestInvalid:
        return u"manifest-invalid"_s;
    case PackageError::VersionMismatch:
        return u"version-mismatch"_s;
    case PackageError::FilesMismatch:
        return u"files-mismatch"_s;
    case PackageError::Cancelled:
        return u"cancelled"_s;
    }
    return u"unknown"_s;
}

int runCheckPackage(const QStringList& arguments)
{
    const QString zip = option(arguments, u"--zip"_s);
    const QString sha256 = option(arguments, u"--sha256"_s);
    const QString version = option(arguments, u"--version"_s);
    const QString work = option(arguments, u"--work"_s);
    const QString out = option(arguments, u"--out"_s);
    if (zip.isEmpty() || sha256.isEmpty() || version.isEmpty() || work.isEmpty() || out.isEmpty())
        return kBadArguments;

    PackageRequest request;
    request.archivePath = QDir::cleanPath(zip);
    request.expectedSha256 = sha256;
    request.stagingDir = QDir::cleanPath(work) + u"/update-"_s + version;
    request.rootDir = QCoreApplication::applicationDirPath();
    request.expectedVersion = version;
    request.expectations = {branding::productName(), branding::executableFileName()};

    const PackageResult result = UpdatePackage::prepare(request);

    QJsonArray files;
    for (const ManifestFile& file : result.manifest.files)
        files.append(
            QJsonObject {{u"path"_s, file.path}, {u"size"_s, file.size}, {u"sha256"_s, file.sha256}});
    const QJsonObject report {{u"ok"_s, result.ok()}, {u"error"_s, errorName(result.error)},
        {u"detail"_s, result.detail}, {u"version"_s, result.manifest.version},
        {u"launcher"_s, result.manifest.launcher}, {u"files"_s, files}};
    if (!writeResult(out, report))
        return kBadArguments;
    return result.ok() ? kOk : kChecksFailed;
}

} // namespace

bool SelfTest::isRequested(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self-test") == 0 || std::strcmp(argv[i], "--check-package") == 0)
            return true;
    }
    return false;
}

int SelfTest::runProgram(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();

    if (arguments.contains(u"--self-test"_s)) {
        const QString out = option(arguments, u"--self-test"_s);
        return out.isEmpty() ? kBadArguments : runSelfTest(out);
    }
    return runCheckPackage(arguments);
}

} // namespace launcher
