#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace launcher {

enum class NetworkError {
    None,
    UrlNotAllowed, ///< The address, or a redirect target, breaks the URL policy. Nothing was sent.
    Network,       ///< No connection: name lookup, refused, reset, TLS failure.
    Timeout,       ///< The server stopped sending data.
    HttpError,     ///< The server answered with an error status.
    RateLimited,   ///< GitHub's request limit is used up.
    TooLarge,      ///< The response is larger than the caller allows.
    TooManyRedirects,
    Aborted, ///< Cancelled by the caller.
};

/// GitHub's request budget as reported in the X-RateLimit headers.
struct RateLimit {
    int limit = -1;
    int remaining = -1;
    QDateTime resetAt; ///< Invalid if the server did not say.

    bool isKnown() const { return limit >= 0 && remaining >= 0; }
    bool operator==(const RateLimit&) const = default;
};

struct FetchOptions {
    qint64 maxBytes = 4 * 1024 * 1024; ///< Larger responses are cut off and rejected.
    int timeoutMs = 15000;             ///< No data for this long counts as a timeout.
    int maxAttempts = 3;               ///< Tries in total for connection problems and server errors.
    int retryDelayMs = 500;            ///< Pause before the second try, doubled for each further one.
    int maxRedirects = 5;
    QByteArray accept = "application/json";
};

struct NetworkResult {
    NetworkError error = NetworkError::None;
    int httpStatus = 0; ///< 0 if no HTTP response arrived.
    QByteArray body;
    QUrl finalUrl;  ///< Address of the last request, after redirects.
    QString detail; ///< Technical detail for the log.
    int attempts = 0;
    RateLimit rateLimit; ///< From the last response, if it carried the headers.

    bool ok() const { return error == NetworkError::None; }
};

class GitHubClient;

/// One running request. Created by GitHubClient::get(); deletes itself after finished().
class GitHubRequest : public QObject {
    Q_OBJECT

public:
    ~GitHubRequest() override;

    /// Cancels the request. finished() is emitted at once with NetworkError::Aborted.
    /// Does nothing if the request is already complete.
    void abort();

signals:
    /// Emitted exactly once, never before get() has returned.
    void finished(const launcher::NetworkResult& result);

private:
    friend class GitHubClient;
    GitHubRequest(GitHubClient* client, const QUrl& url, const FetchOptions& options);

    void begin();
    void send();
    void onFinished();
    void retryOrFail(const NetworkResult& failure, bool retryable);
    void complete(NetworkResult result);

    GitHubClient* m_client;
    QUrl m_originalUrl;
    QUrl m_current;
    FetchOptions m_options;
    QPointer<QNetworkReply> m_reply;
    QTimer* m_retryTimer;
    QElapsedTimer m_clock;
    int m_attempts = 0;
    int m_redirects = 0;
    bool m_done = false;
    bool m_tooLarge = false;
};

/// The only place that talks to the network.
///
/// Every request, and every redirect it is sent on to, is checked against a
/// policy: the launcher only ever contacts a short list of GitHub hosts over
/// HTTPS. Requests are asynchronous and never block the user interface.
class GitHubClient : public QObject {
    Q_OBJECT

public:
    /// Which addresses may be contacted.
    ///
    /// Application code always uses production(). The fields are public so
    /// that tests can point a client at a server on the local machine; no
    /// setting or file can change the policy of a running launcher.
    struct Policy {
        QStringList schemes;
        QStringList hosts;    ///< Exact host names, lowercase.
        bool anyPort = false; ///< Otherwise only the default port of the scheme.

        /// HTTPS to the GitHub hosts that serve the API, raw files and release downloads.
        static Policy production();

        bool allows(const QUrl& url) const;
    };

    explicit GitHubClient(const Policy& policy, QObject* parent = nullptr);

    const Policy& policy() const { return m_policy; }

    /// Starts a GET request. The result arrives through the request's finished() signal.
    /// The request belongs to the client and is deleted after it has finished; keep it in a QPointer.
    GitHubRequest* get(const QUrl& url, const FetchOptions& options = {});

    /// Request budget reported by the last response that carried the headers
    /// (only the API does). Unknown until such a response has arrived.
    RateLimit rateLimit() const { return m_rateLimit; }

signals:
    void rateLimitChanged(const launcher::RateLimit& rateLimit);

private:
    friend class GitHubRequest;
    QNetworkAccessManager* network() const { return m_network; }
    QByteArray userAgent() const { return m_userAgent; }
    void noteRateLimit(const RateLimit& rateLimit);

    Policy m_policy;
    QNetworkAccessManager* m_network;
    QByteArray m_userAgent;
    RateLimit m_rateLimit;
};

} // namespace launcher
