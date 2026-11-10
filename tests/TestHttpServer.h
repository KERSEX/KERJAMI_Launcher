#pragma once

// A minimal HTTP/1.1 server on the local machine for network tests. It answers
// every request through a handler function, so a test decides per request what
// the "server" does: answer, stay silent, or hang up.

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPair>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>
#include <utility>

class TestHttpServer : public QObject {
public:
    struct Request {
        QByteArray method;
        QString path;                          ///< Including the query string.
        QHash<QByteArray, QByteArray> headers; ///< Header names in lowercase.
    };

    struct Response {
        enum class Behavior {
            Respond,
            Hang,               ///< Accept the request and never answer.
            CloseWithoutAnswer, ///< Hang up right after the request.
        };

        int status = 200;
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
        Behavior behavior = Behavior::Respond;

        void addHeader(const QByteArray& name, const QByteArray& value)
        {
            headers.append(qMakePair(name, value));
        }
    };

    using Handler = std::function<Response(const Request&)>;

    explicit TestHttpServer(Handler handler, QObject* parent = nullptr)
        : QObject(parent)
        , m_handler(std::move(handler))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] { acceptConnections(); });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }

    /// Stops listening. Connections made afterwards are refused.
    void close() { m_server.close(); }

    quint16 port() const { return m_server.serverPort(); }

    QUrl url(const QString& path = QStringLiteral("/")) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(path));
    }

    int requestCount() const { return static_cast<int>(m_requests.size()); }
    const QList<Request>& requests() const { return m_requests; }

private:
    void acceptConnections()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            socket->setParent(this);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                m_buffers.remove(socket);
                socket->deleteLater();
            });
        }
    }

    void readRequest(QTcpSocket* socket)
    {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0)
            return;

        const QList<QByteArray> lines = buffer.left(end).split('\n');
        buffer.clear();

        Request request;
        const QList<QByteArray> start = lines.value(0).trimmed().split(' ');
        request.method = start.value(0);
        request.path = QString::fromUtf8(start.value(1));
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines.at(i).indexOf(':');
            if (colon > 0)
                request.headers.insert(
                    lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
        }
        m_requests.append(request);

        const Response response = m_handler(request);
        switch (response.behavior) {
        case Response::Behavior::Hang:
            return; // the socket stays open until the server is destroyed
        case Response::Behavior::CloseWithoutAnswer:
            socket->disconnectFromHost();
            return;
        case Response::Behavior::Respond:
            break;
        }

        QByteArray out =
            "HTTP/1.1 " + QByteArray::number(response.status) + ' ' + reason(response.status) + "\r\n";
        out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
        out += "Connection: close\r\n";
        for (const auto& [name, value] : response.headers)
            out += name + ": " + value + "\r\n";
        out += "\r\n";
        out += response.body;
        socket->write(out);
        socket->disconnectFromHost();
    }

    static QByteArray reason(int status)
    {
        switch (status) {
        case 200:
            return "OK";
        case 302:
            return "Found";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 429:
            return "Too Many Requests";
        case 500:
            return "Internal Server Error";
        case 503:
            return "Service Unavailable";
        default:
            return "Status";
        }
    }

    Handler m_handler;
    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QList<Request> m_requests;
};
