#pragma once

/// @file VersionChecker.h
/// @brief HTTP-based version checker — fetches latest version from emule-qt.org.

#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

namespace eMule {

class VersionChecker : public QObject {
    Q_OBJECT

public:
    explicit VersionChecker(QObject* parent = nullptr);

    /// Trigger a version check.
    /// @param manual  If true, always check regardless of interval.
    void check(bool manual = false);

signals:
    void newVersionAvailable(const QString& version);
    void upToDate();
    void checkFailed();

private:
    void onReplyFinished(QNetworkReply* reply);

    QNetworkAccessManager* m_nam = nullptr;
    bool m_manual = false;
};

} // namespace eMule
