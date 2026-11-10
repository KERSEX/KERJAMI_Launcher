#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>

namespace launcher::HashVerifier {

/// Outcome of comparing a file with the hash a release announced.
enum class Status {
    Match,
    Mismatch,
    NoExpectedHash,      ///< Nothing to compare with. Never counts as success.
    InvalidExpectedHash, ///< The expected value is not 64 hexadecimal characters.
    FileError,
    Cancelled,
};

struct VerifyResult {
    Status status = Status::FileError;
    QString actual; ///< Lowercase hex of the file, empty if it could not be read.
    QString detail;

    bool ok() const { return status == Status::Match; }
};

/// Called with the bytes read so far; may be empty.
using Progress = std::function<void(qint64 processed)>;

/// True for exactly 64 hexadecimal characters, in any case.
bool isValidSha256(const QString& hex);

/// SHA-256 of a file as lowercase hex. Reads in chunks, so even a very large
/// file needs little memory. Returns an empty string on error or cancellation;
/// @p error then tells which of the two.
QString sha256File(const QString& path, QString* error = nullptr, const std::atomic<bool>* cancel = nullptr,
    const Progress& progress = {});

/// Compares a file with an expected SHA-256 value. An empty expected value is
/// reported as NoExpectedHash: a file without a trusted hash is not verified.
VerifyResult verifySha256(const QString& path, const QString& expected,
    const std::atomic<bool>* cancel = nullptr, const Progress& progress = {});

/// Reads the hash of @p fileName from the text of a checksum file.
///
/// Understands the output of sha256sum ("<hash>  <name>" and "<hash> *<name>").
/// A line with only a hash counts when @p allowBareHash is set, which is the
/// case for a file that belongs to exactly one asset ("<asset>.sha256").
/// Returns lowercase hex, or an empty string if no line names the file, the
/// content is malformed, or several lines give different hashes for it.
QString findInChecksumFile(const QByteArray& content, const QString& fileName, bool allowBareHash);

} // namespace launcher::HashVerifier
