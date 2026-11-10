#include "github/GitHubClient.h"

#include "app/Branding.h"
#include "core/Logger.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <algorithm>
#include <chrono>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr QLatin1StringView kApiHost = "api.github.com"_L1;
constexpr auto kApiVersion = "2022-11-28";

bool isRedirectStatus(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

RateLimit parseRateLimit(const QNetworkReply* reply)
{
    bool hasLimit = false;
    bool hasRemaining = false;
    bool hasReset = false;
    const int limit = reply->rawHeader("X-RateLimit-Limit").toInt(&hasLimit);
    const int remaining = reply->rawHeader("X-RateLimit-Remaining").toInt(&hasRemaining);
    const qint64 reset = reply->rawHeader("X-RateLimit-Reset").toLongLong(&hasReset);
    if (!hasLimit || !hasRemaining)
        return {};

    RateLimit result;
    result.limit = limit;
    result.remaining = remaining;
    if (hasReset)
        result.resetAt = QDateTime::fromSecsSinceEpoch(reset).toUTC();
    return result;
}

/// Host and path only. The query string is left out of the log.
QString describeTarget(const QUrl& url)
{
    return url.host() + url.path();
}

QLatin1StringView errorName(NetworkError error)
{
    switch (error) {
    case NetworkError::None:
        return "ok"_L1;
    case NetworkError::UrlNotAllowed:
        return "address not allowed"_L1;
    case NetworkError::Network:
        return "no connection"_L1;
    case NetworkError::Timeout:
        return "timeout"_L1;
    case NetworkError::HttpError:
        return "http error"_L1;
    case NetworkError::RateLimited:
        return "rate limited"_L1;
    case NetworkError::TooLarge:
        return "response too large"_L1;
    case NetworkError::FileError:
        return "file error"_L1;
    case NetworkError::TooManyRedirects:
        return "too many redirects"_L1;
    case NetworkError::Aborted:
        return "aborted"_L1;
    }
    return "unknown"_L1;
}

} // namespace

// ---- GitHubClient ------------------------------------------------------------

GitHubClient::Policy GitHubClient::Policy::production()
{
    Policy policy;
    policy.schemes = {u"https"_s};
    policy.hosts = {
        u"raw.githubusercontent.com"_s,     // registry file
        u"api.github.com"_s,                // releases
        u"github.com"_s,                    // release downloads
        u"objects.githubusercontent.com"_s, // where release downloads are redirected to
        u"release-assets.githubusercontent.com"_s,
    };
    return policy;
}

bool GitHubClient::Policy::allows(const QUrl& url) const
{
    if (!url.isValid() || url.isRelative() || !url.userInfo().isEmpty())
        return false;
    if (!schemes.contains(url.scheme(), Qt::CaseInsensitive))
        return false;
    if (!hosts.contains(url.host().toLower()))
        return false;

    if (!anyPort) {
        const int defaultPort = url.scheme().compare("https"_L1, Qt::CaseInsensitive) == 0 ? 443 : 80;
        if (url.port() != -1 && url.port() != defaultPort)
            return false;
    }
    return true;
}

GitHubClient::GitHubClient(const Policy& policy, QObject* parent)
    : QObject(parent)
    , m_policy(policy)
    , m_network(new QNetworkAccessManager(this))
    , m_userAgent((branding::productName() + u'/' + branding::version()).toUtf8())
{
    // Redirects are followed by GitHubRequest so that every hop can be checked.
    m_network->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
}

GitHubRequest* GitHubClient::get(const QUrl& url, const FetchOptions& options)
{
    auto* request = new GitHubRequest(this, url, options);
    // Deferred, so that the caller can connect to finished() first.
    QMetaObject::invokeMethod(request, &GitHubRequest::begin, Qt::QueuedConnection);
    return request;
}

void GitHubClient::noteRateLimit(const RateLimit& rateLimit)
{
    if (rateLimit == m_rateLimit)
        return;
    m_rateLimit = rateLimit;
    emit rateLimitChanged(rateLimit);
}

// ---- GitHubRequest -----------------------------------------------------------

GitHubRequest::GitHubRequest(GitHubClient* client, const QUrl& url, const FetchOptions& options)
    : QObject(client)
    , m_client(client)
    , m_originalUrl(url)
    , m_current(url)
    , m_options(options)
    , m_retryTimer(new QTimer(this))
{
    m_options.maxAttempts = std::max(1, m_options.maxAttempts);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &GitHubRequest::send);
}

GitHubRequest::~GitHubRequest()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void GitHubRequest::begin()
{
    if (m_done)
        return;
    m_clock.start();

    if (!m_client->policy().allows(m_originalUrl)) {
        NetworkResult result;
        result.error = NetworkError::UrlNotAllowed;
        result.finalUrl = m_originalUrl;
        result.detail = u"address is not on the allow list: %1"_s.arg(describeTarget(m_originalUrl));
        complete(result);
        return;
    }

    m_attempts = 1;
    send();
}

void GitHubRequest::send()
{
    if (m_done)
        return;

    QNetworkRequest request(m_current);
    request.setRawHeader("User-Agent", m_client->userAgent());
    request.setRawHeader("Accept", m_options.accept);
    if (m_current.host() == kApiHost)
        request.setRawHeader("X-GitHub-Api-Version", kApiVersion);
    request.setTransferTimeout(std::chrono::milliseconds(m_options.timeoutMs));

    m_tooLarge = false;
    m_writeFailed = false;
    m_written = 0;
    m_file.reset();
    if (!m_options.saveToFile.isEmpty()) {
        // Every attempt, and every redirect hop, starts the file anew.
        auto file = std::make_unique<QFile>(m_options.saveToFile);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            NetworkResult result;
            result.error = NetworkError::FileError;
            result.finalUrl = m_current;
            result.detail = u"cannot open the target file: %1"_s.arg(file->errorString());
            complete(result);
            return;
        }
        m_file = std::move(file);
    }

    QNetworkReply* reply = m_client->network()->get(request);
    m_reply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
        if (m_reply != reply)
            return;
        if (received > m_options.maxBytes || total > m_options.maxBytes) {
            m_tooLarge = true;
            reply->abort();
            return;
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] { drain(reply); });
    connect(reply, &QNetworkReply::finished, this, &GitHubRequest::onFinished);
}

/// Moves what has arrived of a successful answer into the target file.
/// Other answers are small error pages and stay in the reply.
void GitHubRequest::drain(QNetworkReply* reply)
{
    if (!m_file || m_writeFailed || m_reply != reply)
        return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status < 200 || status >= 300)
        return;

    // The length the server announced, -1 if it did not say.
    const QVariant announced = reply->header(QNetworkRequest::ContentLengthHeader);
    const qint64 total = announced.isValid() ? announced.toLongLong() : -1;
    if (total > m_options.maxBytes) {
        m_tooLarge = true;
        reply->abort();
        return;
    }

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty())
        return;
    if (m_file->write(chunk) != chunk.size()) {
        m_writeFailed = true;
        m_fileError = m_file->errorString();
        reply->abort();
        return;
    }
    m_written += chunk.size();
    if (m_written > m_options.maxBytes) {
        m_tooLarge = true;
        reply->abort();
        return;
    }

    // Counted here, from what has been written. Qt's own progress signal can
    // lag behind the data by seconds when a transfer stalls.
    emit downloadProgress(m_written, total);
}

void GitHubRequest::onFinished()
{
    if (m_done || !m_reply)
        return;
    QNetworkReply* reply = m_reply.data();
    drain(reply); // whatever arrived after the last readyRead()
    m_reply.clear();
    reply->deleteLater();

    NetworkResult result;
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.finalUrl = m_current;
    result.rateLimit = parseRateLimit(reply);
    if (result.rateLimit.isKnown())
        m_client->noteRateLimit(result.rateLimit);
    const int status = result.httpStatus;

    if (m_writeFailed) {
        result.error = NetworkError::FileError;
        result.detail = u"cannot write the target file: %1"_s.arg(m_fileError);
        complete(result);
        return;
    }

    if (m_tooLarge) {
        result.error = NetworkError::TooLarge;
        result.detail = u"response is larger than %1 bytes"_s.arg(m_options.maxBytes);
        complete(result);
        return;
    }

    if (isRedirectStatus(status)) {
        // Read as raw text: the parsed header is empty for relative addresses.
        const QByteArray location = reply->rawHeader("Location").trimmed();
        const QUrl target = location.isEmpty() ? QUrl() : m_current.resolved(QUrl::fromEncoded(location));
        if (!target.isValid()) { // also true for an empty URL, i.e. no Location header
            result.error = NetworkError::HttpError;
            result.detail = u"HTTP %1 without a usable Location header"_s.arg(status);
            complete(result);
            return;
        }
        if (++m_redirects > m_options.maxRedirects) {
            result.error = NetworkError::TooManyRedirects;
            result.detail = u"more than %1 redirects"_s.arg(m_options.maxRedirects);
            complete(result);
            return;
        }
        if (!m_client->policy().allows(target)) {
            result.error = NetworkError::UrlNotAllowed;
            result.detail = u"redirect target is not on the allow list: %1"_s.arg(describeTarget(target));
            complete(result);
            return;
        }
        qCInfo(lcNetwork).noquote() << "GET" << describeTarget(m_current) << "->" << status << "redirect to"
                                    << describeTarget(target);
        m_current = target;
        send();
        return;
    }

    const QNetworkReply::NetworkError networkError = reply->error();

    // No HTTP answer at all, or one that broke off half way.
    const bool answered = status >= 100;
    if (!answered || (status >= 200 && status < 300 && networkError != QNetworkReply::NoError)) {
        result.detail = reply->errorString();
        if (networkError == QNetworkReply::TimeoutError) {
            result.error = NetworkError::Timeout;
            retryOrFail(result, true);
        } else {
            result.error = NetworkError::Network;
            // A certificate or protocol failure will not heal by trying again.
            retryOrFail(result, networkError != QNetworkReply::SslHandshakeFailedError);
        }
        return;
    }

    if (status >= 200 && status < 300) {
        if (m_file) {
            result.bytesReceived = m_written;
            if (m_written > m_options.maxBytes) {
                result.error = NetworkError::TooLarge;
                result.detail = u"response is larger than %1 bytes"_s.arg(m_options.maxBytes);
            } else if (!m_file->flush()) {
                result.error = NetworkError::FileError;
                result.detail = u"cannot write the target file: %1"_s.arg(m_file->errorString());
            }
        } else {
            result.body = reply->readAll();
            result.bytesReceived = result.body.size();
            if (result.body.size() > m_options.maxBytes) {
                result.error = NetworkError::TooLarge;
                result.body.clear();
                result.detail = u"response is larger than %1 bytes"_s.arg(m_options.maxBytes);
            }
        }
        complete(result);
        return;
    }

    result.detail = u"HTTP %1"_s.arg(status);
    const bool budgetUsedUp = result.rateLimit.isKnown() && result.rateLimit.remaining == 0;
    if (status == 429 || (status == 403 && (budgetUsedUp || reply->hasRawHeader("Retry-After")))) {
        result.error = NetworkError::RateLimited;
        if (result.rateLimit.resetAt.isValid())
            result.detail += u", limit resets at %1"_s.arg(result.rateLimit.resetAt.toString(Qt::ISODate));
        complete(result);
        return;
    }

    result.error = NetworkError::HttpError;
    retryOrFail(result, status == 408 || status >= 500);
}

void GitHubRequest::retryOrFail(const NetworkResult& failure, bool retryable)
{
    if (!retryable || m_attempts >= m_options.maxAttempts) {
        complete(failure);
        return;
    }

    const int delay = m_options.retryDelayMs * (1 << std::min(m_attempts - 1, 10));
    qCInfo(lcNetwork).noquote() << "GET" << describeTarget(m_current) << "failed (" << failure.detail
                                << "), try" << m_attempts + 1 << "of" << m_options.maxAttempts << "in"
                                << delay << "ms";
    ++m_attempts;
    m_retryTimer->start(delay);
}

void GitHubRequest::complete(NetworkResult result)
{
    if (m_done)
        return;
    m_done = true;
    m_retryTimer->stop();
    if (m_file) {
        m_file->close(); // the file stays; whoever asked decides what to do with it
        m_file.reset();
    }
    result.attempts = m_attempts;
    if (!result.finalUrl.isValid())
        result.finalUrl = m_originalUrl;

    const QString target = describeTarget(result.finalUrl);
    if (result.ok()) {
        qCInfo(lcNetwork).noquote() << "GET" << target << "->" << result.httpStatus << '('
                                    << result.bytesReceived << "bytes," << m_clock.elapsed() << "ms )";
    } else if (result.error == NetworkError::Aborted) {
        qCInfo(lcNetwork).noquote() << "GET" << target << "aborted";
    } else {
        qCWarning(lcNetwork).noquote() << "GET" << target << "failed:" << errorName(result.error) << '-'
                                       << result.detail << '(' << result.attempts << "attempts )";
    }

    emit finished(result);
    deleteLater();
}

void GitHubRequest::abort()
{
    if (m_done)
        return;
    if (m_reply) {
        // Disconnect first: abort() would report back through onFinished().
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }

    NetworkResult result;
    result.error = NetworkError::Aborted;
    result.finalUrl = m_current;
    complete(result);
}

} // namespace launcher
