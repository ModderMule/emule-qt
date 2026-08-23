#pragma once

/// @file URLClient.h
/// @brief HTTP download client — subclass of UpDownClient for URL-based sources.
///
/// Port of MFC CUrlClient (srchybrid/URLClient.cpp).
/// Handles HTTP GET-based file downloads from web servers rather than
/// ed2k protocol peers.

#include "client/UpDownClient.h"

#include <QList>

namespace eMule {

class HostResolver;

class URLClient : public UpDownClient {
    Q_OBJECT

public:
    explicit URLClient(QObject* parent = nullptr);
    ~URLClient() override;

    /// Parse URL into host/port/path components. Returns false on invalid URL.
    /// @param fromAddr  Already-known address of the host (either family), or null to
    ///                  resolve the hostname at connect time.
    bool setUrl(const QString& url, const Address& fromAddr);

    /// Network-byte-order IPv4 convenience overload.
    bool setUrl(const QString& url, uint32 fromIP = 0);

    /// Set the file this URL client is downloading.
    void setRequestFile(PartFile* reqFile);

    // -- Overrides -----------------------------------------------------------

    bool tryToConnect(bool ignoreMaxCon = false, bool noCallbacks = false) override;
    void connectionEstablished() override;
    bool disconnected(const QString& reason, bool fromSocket = false) override;
    void sendFileRequest() override;
    void sendBlockRequests() override;
    void sendCancelTransfer() override;
    void checkDownloadTimeout() override;
    void onSocketConnected(int errorCode) override;

    // The three HTTP hooks are virtual so HttpCacheClient can slot a decrypt
    // stage in without duplicating the socket, throttling and block-writing
    // machinery below.

    /// Build and send HTTP GET request with Range header.
    virtual bool sendHttpBlockRequests();

    /// Process HTTP response headers. Returns true if valid response.
    virtual bool processHttpDownResponse(const QList<QByteArray>& headers);

    /// Process HTTP response body data.
    virtual bool processHttpDownResponseBody(const uint8* data, uint32 size);

    /// Process received HTTP data as file block.
    void processHttpBlockPacket(const uint8* data, uint32 size);

    [[nodiscard]] bool isEd2kClient() const override { return false; }

    [[nodiscard]] const QString& urlHost() const { return m_urlHost; }
    [[nodiscard]] uint16 urlPort() const { return m_urlPort; }
    [[nodiscard]] const QByteArray& urlPath() const { return m_urlPathLocal; }

protected:
    /// Build the request line and the common headers, up to but not including
    /// Range and the terminating blank line. Subclasses append their own.
    [[nodiscard]] QByteArray buildGetHeader() const;

    /// Send a pre-built raw request over the socket.
    bool sendRawRequest(const QByteArray& request);

    /// Status code of an HTTP response, or -1 when the line is not one.
    [[nodiscard]] static int parseStatusCode(const QByteArray& statusLine);

    /// Case-insensitive header lookup over the accumulated header lines.
    [[nodiscard]] static QByteArray headerValue(const QList<QByteArray>& headers,
                                                const char* name);

    /// Where the next body byte belongs in the file. Set from Content-Range and
    /// advanced by processHttpBlockPacket.
    [[nodiscard]] uint64 rangeStart() const { return m_rangeStart; }
    void setRangeStart(uint64 pos) { m_rangeStart = pos; }

private:
    void sendHelloPacket() override {} // no-op for HTTP
    void connectToHost(); // create socket and initiate TCP connection

    /// Gate every address we are about to dial through the shared peer rules.
    ///
    /// A URL host is chosen by somebody else — a peer's HTTP Cache offer, a Kad chunk
    /// record, an ed2k link — so the address behind it is no more trusted than a peer
    /// address, and DNS is part of the attack surface: a name that resolves to
    /// 127.0.0.1 or fd00::1 is exactly the request a literal one is screened for.
    /// @return true when the connection may proceed; false after disconnecting.
    bool acceptResolvedAddress(const Address& addr);

    HostResolver* m_hostResolver = nullptr;   // created on first hostname connect
    QString m_urlHost;
    uint16 m_urlPort = 80;
    QByteArray m_urlPathLocal;
    uint64 m_rangeStart = 0;
    uint64 m_rangeEnd = 0;
};

} // namespace eMule
