#pragma once

/// @file HttpDefaults.h
/// @brief What every outgoing eMuleQt HTTP request carries, whoever builds it.
///
/// The identity used to be a constant that each caller remembered to apply, and
/// two of them did not. Neither failure is quiet: URLClient's hand-built GET —
/// the request that fetches HTTP Cache chunks and every plain HTTP source — went
/// out with no User-Agent at all, which is what a default WAF ruleset challenges,
/// and the symptom is a cache that looks dead rather than a client that looks
/// blocked. A forgotten QNetworkRequest is arguably worse: Qt fills in
/// `Mozilla/5.0`, so the request claims to be a browser, and an operator who
/// allow-listed `eMule*` still does not see us.
///
/// So the decision lives here instead. Two forms, because there are two kinds of
/// caller: QNetworkAccessManager users pass a QNetworkRequest, and URLClient
/// assembles its request as raw bytes over an EMSocket. They sit together so the
/// two can never drift apart.
///
/// Deliberately *not* here: the transfer timeout, which is per-caller (a stall
/// detector on a throttled upload is not a deadline on a probe), and
/// Authorization, which only the two callers holding a credential may send.

#include "app/AppConfig.h"

#include <QByteArray>
#include <QNetworkRequest>
#include <QUrl>

namespace eMule::Http {

/// Apply the shared settings to an already-built request.
inline void applyDefaults(QNetworkRequest& request)
{
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    // Qt 6 already defaults to this; stating it keeps the behaviour independent of
    // the Qt version, and the "no less safe" rule is what stops an https URL being
    // silently redirected down to http.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QVariant::fromValue(QNetworkRequest::NoLessSafeRedirectPolicy));
}

/// A request for @p url with those settings already on it.
[[nodiscard]] inline QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    applyDefaults(request);
    return request;
}

/// `User-Agent: eMuleQt/<version>\r\n`, for a request assembled as raw bytes.
/// Latin-1 because a header field value is bytes, and the version string is ASCII.
[[nodiscard]] inline QByteArray userAgentHeaderLine()
{
    return "User-Agent: " + kUserAgent.toLatin1() + "\r\n";
}

} // namespace eMule::Http
