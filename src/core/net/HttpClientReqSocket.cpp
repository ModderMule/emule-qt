/// @file HttpClientReqSocket.cpp
/// @brief HTTP protocol over EMSocket — replaces MFC CHttpClientReqSocket.

#include "net/HttpClientReqSocket.h"
#include "client/URLClient.h"
#include "utils/Log.h"

#include <QByteArray>
#include <QList>

namespace eMule {

// ---------------------------------------------------------------------------
// HttpClientReqSocket
// ---------------------------------------------------------------------------

HttpClientReqSocket::HttpClientReqSocket(UpDownClient* client, QObject* parent)
    : ClientReqSocket(client, parent)
{
    m_httpState = HttpSocketState::RecvExpected;
}

HttpClientReqSocket::~HttpClientReqSocket() = default;

void HttpClientReqSocket::setHttpState(HttpSocketState state)
{
    m_httpState = state;
}

void HttpClientReqSocket::clearHttpHeaders()
{
    m_httpHeaders.clear();
    m_currentHeaderLine.clear();
    m_httpHeadersSize = 0;
}

// ---------------------------------------------------------------------------
// Raw data handling
// ---------------------------------------------------------------------------

void HttpClientReqSocket::dataReceived(const uint8* data, uint32 size)
{
    if (size == 0)
        return;

    processHttpPacket(data, size);
}

bool HttpClientReqSocket::processHttpPacket(const uint8* data, uint32 size)
{
    if (m_httpState == HttpSocketState::RecvBody) {
        // Already in body mode — forward directly
        return processHttpResponseBody(data, size);
    }

    if (m_httpState == HttpSocketState::RecvExpected ||
        m_httpState == HttpSocketState::RecvHeaders) {
        m_httpState = HttpSocketState::RecvHeaders;

        const uint8* bodyStart = nullptr;
        int bodySize = 0;

        processHttpHeaderPacket(reinterpret_cast<const char*>(data), size,
                                bodyStart, bodySize);

        if (bodyStart != nullptr) {
            // Headers complete — process response
            if (!processHttpResponse()) {
                disconnect(QStringLiteral("HTTP response processing failed"));
                return false;
            }

            m_httpState = HttpSocketState::RecvBody;

            // If body data was included in header packet, process it
            if (bodySize > 0) {
                if (!processHttpResponseBody(bodyStart, static_cast<uint32>(bodySize))) {
                    disconnect(QStringLiteral("HTTP body processing failed"));
                    return false;
                }
            }
        }
        return true;
    }

    return true;
}

void HttpClientReqSocket::processHttpHeaderPacket(const char* data, uint32 size,
                                                   const uint8*& bodyStart, int& bodySize)
{
    bodyStart = nullptr;
    bodySize = 0;

    for (uint32 i = 0; i < size; ++i) {
        char c = data[i];

        if (c == '\n') {
            // Line complete — check if it's the empty line marking end of headers
            QByteArray line = m_currentHeaderLine.trimmed();
            m_currentHeaderLine.clear();

            if (line.isEmpty()) {
                // End of headers — everything after is body
                bodyStart = reinterpret_cast<const uint8*>(data + i + 1);
                bodySize = static_cast<int>(size - i - 1);
                return;
            }

            m_httpHeadersSize += line.size();
            if (m_httpHeadersSize > kMaxTotalHeaderSize) {
                logWarning(QStringLiteral("HTTP headers too large (%1 bytes)")
                               .arg(m_httpHeadersSize));
                disconnect(QStringLiteral("HTTP headers too large"));
                return;
            }

            m_httpHeaders.push_back(std::move(line));
        } else if (c != '\r') {
            // Accumulate character into current line
            if (m_currentHeaderLine.size() < kMaxHeaderLineSize)
                m_currentHeaderLine.append(c);
        }
    }
}

// ---------------------------------------------------------------------------
// Virtual response handlers (base implementation)
// ---------------------------------------------------------------------------

bool HttpClientReqSocket::processHttpResponse()
{
    // Parse status code from first header line
    if (m_httpHeaders.empty())
        return false;

    const QByteArray& statusLine = m_httpHeaders[0];
    // Expected: "HTTP/1.x NNN ..."
    auto spacePos = statusLine.indexOf(static_cast<char>(' '));
    if (spacePos < 0)
        return false;

    QByteArray codeStr = statusLine.mid(spacePos + 1, 3);
    bool ok = false;
    int statusCode = codeStr.toInt(&ok);
    if (!ok)
        return false;

    emit httpResponseReceived(statusCode);
    return (statusCode >= 200 && statusCode < 300);
}

bool HttpClientReqSocket::processHttpResponseBody(const uint8* data, uint32 size)
{
    emit httpBodyDataReceived(data, size);
    return true;
}

bool HttpClientReqSocket::processHttpRequest()
{
    // Client sockets don't expect incoming HTTP requests
    logWarning(QStringLiteral("Unexpected HTTP request on client socket"));
    return false;
}

// ---------------------------------------------------------------------------
// HttpClientDownSocket
// ---------------------------------------------------------------------------

HttpClientDownSocket::HttpClientDownSocket(UpDownClient* client, QObject* parent)
    : HttpClientReqSocket(client, parent)
{
}

bool HttpClientDownSocket::processHttpResponse()
{
    if (m_httpHeaders.empty())
        return false;

    // Parse status code
    const QByteArray& statusLine = m_httpHeaders[0];
    auto spacePos = statusLine.indexOf(static_cast<char>(' '));
    if (spacePos < 0)
        return false;

    QByteArray codeStr = statusLine.mid(spacePos + 1, 3);
    bool ok = false;
    int statusCode = codeStr.toInt(&ok);
    if (!ok)
        return false;

    emit httpResponseReceived(statusCode);

    // Delegate response processing to URLClient
    if (auto* urlClient = dynamic_cast<URLClient*>(m_client)) {
        QList<QByteArray> headerList(m_httpHeaders.begin(), m_httpHeaders.end());
        return urlClient->processHttpDownResponse(headerList);
    }

    return (statusCode >= 200 && statusCode < 300);
}

bool HttpClientDownSocket::processHttpResponseBody(const uint8* data, uint32 size)
{
    if (auto* urlClient = dynamic_cast<URLClient*>(m_client))
        return urlClient->processHttpDownResponseBody(data, size);

    emit httpBodyDataReceived(data, size);
    return true;
}

bool HttpClientDownSocket::processHttpRequest()
{
    logWarning(QStringLiteral("Unexpected HTTP request on download socket"));
    return false;
}

} // namespace eMule
