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

class URLClient : public UpDownClient {
    Q_OBJECT

public:
    explicit URLClient(QObject* parent = nullptr);
    ~URLClient() override;

    /// Parse URL into host/port/path components. Returns false on invalid URL.
    bool setUrl(const QString& url, uint32 fromIP = 0);

    /// Set the file this URL client is downloading.
    void setRequestFile(PartFile* reqFile);

    // -- Overrides -----------------------------------------------------------

    bool tryToConnect(bool ignoreMaxCon = false) override;
    void connectionEstablished() override;
    bool disconnected(const QString& reason, bool fromSocket = false) override;
    void sendFileRequest() override;
    void sendBlockRequests() override;
    void sendCancelTransfer() override;
    void checkDownloadTimeout() override;
    void onSocketConnected(int errorCode) override;

    /// Build and send HTTP GET request with Range header.
    bool sendHttpBlockRequests();

    /// Process HTTP response headers. Returns true if valid response.
    bool processHttpDownResponse(const QList<QByteArray>& headers);

    /// Process HTTP response body data.
    bool processHttpDownResponseBody(const uint8* data, uint32 size);

    /// Process received HTTP data as file block.
    void processHttpBlockPacket(const uint8* data, uint32 size);

    [[nodiscard]] bool isEd2kClient() const override { return false; }

    [[nodiscard]] const QString& urlHost() const { return m_urlHost; }
    [[nodiscard]] uint16 urlPort() const { return m_urlPort; }
    [[nodiscard]] const QByteArray& urlPath() const { return m_urlPathLocal; }

private:
    void sendHelloPacket() override {} // no-op for HTTP
    void connectToHost(); // create socket and initiate TCP connection

    QString m_urlHost;
    uint16 m_urlPort = 80;
    QByteArray m_urlPathLocal;
    uint64 m_rangeStart = 0;
    uint64 m_rangeEnd = 0;
};

} // namespace eMule
