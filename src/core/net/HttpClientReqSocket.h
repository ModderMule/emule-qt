#pragma once

/// @file HttpClientReqSocket.h
/// @brief HTTP protocol over EMSocket — replaces MFC CHttpClientReqSocket.
///
/// Inherits ClientReqSocket and operates in raw data mode. Parses HTTP
/// headers and body, delegating response processing to virtual methods.

#include "net/ClientReqSocket.h"

#include <QByteArray>

#include <vector>

namespace eMule {

class UpDownClient;

// ---------------------------------------------------------------------------
// HTTP socket states
// ---------------------------------------------------------------------------

enum class HttpSocketState : uint8 {
    Unknown      = 0,
    RecvExpected = 1,   ///< Waiting for HTTP data.
    RecvHeaders  = 2,   ///< Receiving HTTP headers.
    RecvBody     = 3    ///< Receiving HTTP body.
};

// ---------------------------------------------------------------------------
// HttpClientReqSocket
// ---------------------------------------------------------------------------

/// HTTP protocol wrapper over EMSocket (raw data mode).
///
/// Parses HTTP response headers line by line, then delegates to
/// virtual processHttpResponse() and processHttpResponseBody().
class HttpClientReqSocket : public ClientReqSocket {
    Q_OBJECT

public:
    explicit HttpClientReqSocket(UpDownClient* client = nullptr, QObject* parent = nullptr);
    ~HttpClientReqSocket() override;

    /// Always true — operates in raw data mode (no ED2K framing).
    [[nodiscard]] bool isRawDataMode() const override { return true; }

    /// Get the current HTTP state.
    [[nodiscard]] HttpSocketState httpState() const { return m_httpState; }

    /// Set the HTTP state.
    void setHttpState(HttpSocketState state);

    /// Clear accumulated HTTP headers.
    void clearHttpHeaders();

    /// Access the parsed headers.
    [[nodiscard]] const std::vector<QByteArray>& httpHeaders() const { return m_httpHeaders; }

signals:
    /// HTTP response headers fully received.
    void httpResponseReceived(int statusCode);

    /// HTTP response body data received.
    void httpBodyDataReceived(const uint8* data, uint32 size);

protected:
    /// Called when raw data is received (override from EMSocket).
    void dataReceived(const uint8* data, uint32 size) override;

    /// Process complete HTTP headers. Return false to disconnect.
    virtual bool processHttpResponse();

    /// Process HTTP body data. Return false to disconnect.
    virtual bool processHttpResponseBody(const uint8* data, uint32 size);

    /// Process an incoming HTTP request (unexpected for client socket).
    virtual bool processHttpRequest();

    HttpSocketState m_httpState = HttpSocketState::Unknown;
    QByteArray m_currentHeaderLine;          ///< Partial header line buffer.
    std::vector<QByteArray> m_httpHeaders;   ///< Complete header lines.
    int m_httpHeadersSize = 0;               ///< Total bytes in all headers.

private:
    bool processHttpPacket(const uint8* data, uint32 size);
    void processHttpHeaderPacket(const char* data, uint32 size,
                                 const uint8*& bodyStart, int& bodySize);

    static constexpr int kMaxHeaderLineSize = 1024;
    static constexpr int kMaxTotalHeaderSize = 2048;
};

// ---------------------------------------------------------------------------
// HttpClientDownSocket
// ---------------------------------------------------------------------------

/// HTTP download socket — delegates response/body to UpDownClient.
class HttpClientDownSocket : public HttpClientReqSocket {
    Q_OBJECT

public:
    explicit HttpClientDownSocket(UpDownClient* client = nullptr, QObject* parent = nullptr);
    ~HttpClientDownSocket() override = default;

protected:
    bool processHttpResponse() override;
    bool processHttpResponseBody(const uint8* data, uint32 size) override;
    bool processHttpRequest() override;
};

} // namespace eMule
