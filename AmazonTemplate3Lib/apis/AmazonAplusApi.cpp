// GCC 13 ICE workaround: coroutines with non-trivially-destructible locals in
// the frame trigger a bug in build_special_member_call (cp/call.cc:11096).
// Forcing O1 avoids the affected code path in the coroutine lowering pass.
// In addition, every coroutine in this translation unit returns
// QCoro::Task<void> and communicates its result via an output parameter, to
// avoid co_awaiting a Task<T> whose T is non-trivially destructible (another
// form of the same GCC 13 bug).
#pragma GCC optimize("O1")
#include "AmazonAplusApi.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QCryptographicHash>
#include <QHttpMultiPart>

#include <QCoro/QCoroNetworkReply>

// ---------------------------------------------------------------------------
// Marketplace → endpoint / region / locale tables
// ---------------------------------------------------------------------------

QString AmazonAplusApi::endpointForMarketplace(const QString &marketplaceId)
{
    static const QStringList euIds = {
        QStringLiteral("A1F83G8C2ARO7P"), // UK
        QStringLiteral("A1PA6795UKMFR9"), // DE
        QStringLiteral("A13V1IB3VIYZZH"), // FR
        QStringLiteral("APJ6JRA9NG5V4"),  // IT
        QStringLiteral("A1RKKUPIHCS9HS"), // ES
        QStringLiteral("A1805IZSGTT6HS"), // NL
        QStringLiteral("A2NODRKZP88ZB9"), // SE
        QStringLiteral("A1C3SOZRARQ6R3"), // PL
        QStringLiteral("AMEN7PMS3EDWL"),  // BE
        QStringLiteral("A28R8C7NBKEWEA"), // IE
        QStringLiteral("A33AVAJ2PDY3EV")  // TR
    };
    static const QStringList naIds = {
        QStringLiteral("ATVPDKIKX0DER"),  // US
        QStringLiteral("A2EUQ1WTGCTBG2"), // CA
        QStringLiteral("A1AM78C64UM0Y8")  // MX
    };
    static const QStringList jpIds = {
        QStringLiteral("A1VC38T7YXB528")  // JP
    };
    if (euIds.contains(marketplaceId))
        return QStringLiteral("sellingpartnerapi-eu.amazon.com");
    if (naIds.contains(marketplaceId))
        return QStringLiteral("sellingpartnerapi-na.amazon.com");
    if (jpIds.contains(marketplaceId))
        return QStringLiteral("sellingpartnerapi-fe.amazon.com");
    return QStringLiteral("sellingpartnerapi-eu.amazon.com");
}

QString AmazonAplusApi::lwaRegionForMarketplace(const QString &marketplaceId)
{
    static const QStringList naIds = {
        QStringLiteral("ATVPDKIKX0DER"),
        QStringLiteral("A2EUQ1WTGCTBG2"),
        QStringLiteral("A1AM78C64UM0Y8")
    };
    static const QStringList jpIds = {
        QStringLiteral("A1VC38T7YXB528")
    };
    if (naIds.contains(marketplaceId)) return QStringLiteral("NA");
    if (jpIds.contains(marketplaceId)) return QStringLiteral("JP");
    return QStringLiteral("EU");
}

QString AmazonAplusApi::localeForMarketplace(const QString &marketplaceId)
{
    // Amazon A+ Content locale format: RFC 5646 with hyphen separator (en-GB, not en_GB).
    // Pattern enforced by API: ^[a-z]{2,}-[A-Z0-9]{2,}$
    static const QHash<QString, QString> kMap = {
        {QStringLiteral("A1F83G8C2ARO7P"), QStringLiteral("en-GB")},
        {QStringLiteral("A1PA6795UKMFR9"), QStringLiteral("de-DE")},
        {QStringLiteral("A13V1IB3VIYZZH"), QStringLiteral("fr-FR")},
        {QStringLiteral("APJ6JRA9NG5V4"),  QStringLiteral("it-IT")},
        {QStringLiteral("A1RKKUPIHCS9HS"), QStringLiteral("es-ES")},
        {QStringLiteral("A1805IZSGTT6HS"), QStringLiteral("nl-NL")},
        {QStringLiteral("A2NODRKZP88ZB9"), QStringLiteral("sv-SE")},
        {QStringLiteral("A1C3SOZRARQ6R3"), QStringLiteral("pl-PL")},
        {QStringLiteral("AMEN7PMS3EDWL"),  QStringLiteral("fr-BE")},
        {QStringLiteral("A28R8C7NBKEWEA"), QStringLiteral("en-IE")},
        {QStringLiteral("A33AVAJ2PDY3EV"), QStringLiteral("tr-TR")},
        {QStringLiteral("ATVPDKIKX0DER"),  QStringLiteral("en-US")},
        {QStringLiteral("A2EUQ1WTGCTBG2"), QStringLiteral("en-CA")},
        {QStringLiteral("A1AM78C64UM0Y8"), QStringLiteral("es-MX")},
        {QStringLiteral("A1VC38T7YXB528"), QStringLiteral("ja-JP")},
    };
    return kMap.value(marketplaceId, QStringLiteral("en-GB"));
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AmazonAplusApi::AmazonAplusApi(const QString &clientId,
                                const QString &clientSecret,
                                const QString &euRefreshToken,
                                const QString &naRefreshToken,
                                const QString &jpRefreshToken,
                                QObject *parent)
    : QObject(parent)
    , m_clientId(clientId)
    , m_clientSecret(clientSecret)
    , m_euRefresh(euRefreshToken)
    , m_naRefresh(naRefreshToken)
    , m_jpRefresh(jpRefreshToken)
{
}

AmazonAplusApi::~AmazonAplusApi() = default;

QNetworkAccessManager *AmazonAplusApi::_nam()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setTransferTimeout(30'000);
    }
    return m_nam;
}

// ---------------------------------------------------------------------------
// Helpers (file-local)
// ---------------------------------------------------------------------------

static QString rawHeaderCI(QNetworkReply *reply, const QByteArray &name)
{
    const QByteArray lower = name.toLower();
    for (const QByteArray &h : reply->rawHeaderList()) {
        if (h.toLower() == lower)
            return QString::fromUtf8(reply->rawHeader(h));
    }
    return {};
}

static void writeAplusDiagnostic(const QString &tag,
                                  const QString &requestSummary,
                                  const QByteArray &requestBody,
                                  int httpStatus,
                                  const QString &requestId,
                                  const QByteArray &responseBody)
{
    const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
    const QString filePath = QStringLiteral("/tmp/sp-api-aplus-%1-%2.txt").arg(tag, ts);
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "AmazonAplusApi: could not write diagnostic file" << filePath;
        return;
    }
    QTextStream s(&f);
    s << "=== REQUEST ===\n"
      << requestSummary << "\n\n";
    if (!requestBody.isEmpty())
        s << "Body:\n" << QString::fromUtf8(requestBody) << "\n\n";
    s << "=== RESPONSE ===\n"
      << "HTTP " << httpStatus << "\n"
      << "x-amzn-RequestId: " << requestId << "\n\n"
      << QString::fromUtf8(responseBody) << "\n";
    qDebug() << "AmazonAplusApi: diagnostic file written to" << filePath;
}

// ---------------------------------------------------------------------------
// LWA token
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonAplusApi::_getAccessToken(QString lwaRegion, QString *out)
{
    const bool isNa = (lwaRegion == QStringLiteral("NA"));
    const bool isJp = (lwaRegion == QStringLiteral("JP"));
    QString   *const pToken   = isNa ? &m_tokenNa  : (isJp ? &m_tokenJp  : &m_tokenEu);
    QDateTime *const pExpiry  = isNa ? &m_expiryNa : (isJp ? &m_expiryJp : &m_expiryEu);
    const QString *const pRefresh = isNa ? &m_naRefresh
                                          : (isJp ? &m_jpRefresh : &m_euRefresh);

    if (!pToken->isEmpty() && pExpiry->isValid()
            && QDateTime::currentDateTimeUtc() < *pExpiry) {
        *out = *pToken;
        co_return;
    }

    if (pRefresh->isEmpty()) {
        qDebug() << "AmazonAplusApi: no refresh token for region" << lwaRegion;
        co_return;
    }

    QUrl url(QStringLiteral("https://api.amazon.com/auth/o2/token"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"),    QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("refresh_token"), *pRefresh);
    body.addQueryItem(QStringLiteral("client_id"),     m_clientId);
    body.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    const QByteArray payload = body.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply *reply = _nam()->post(req, payload);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    const QJsonObject obj = QJsonDocument::fromJson(data).object();
    *pToken = obj.value(QStringLiteral("access_token")).toString();
    if (pToken->isEmpty()) {
        const QString errCode = obj.value(QStringLiteral("error")).toString();
        const QString errDesc = obj.value(QStringLiteral("error_description")).toString();
        m_lastError = errDesc.isEmpty() ? errCode : errDesc;
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("LWA token exchange failed");
        qWarning() << "AmazonAplusApi: LWA token exchange failed for region" << lwaRegion
                   << ":" << m_lastError;
    } else {
        qDebug() << "AmazonAplusApi: LWA token obtained for region" << lwaRegion;
    }
    const int expiresIn = obj.value(QStringLiteral("expires_in")).toInt(3600);
    const int cacheSecs = qMin(expiresIn - 300, 55 * 60);
    *pExpiry = QDateTime::currentDateTimeUtc().addSecs(qMax(cacheSecs, 60));
    *out = *pToken;
    co_return;
}

// ---------------------------------------------------------------------------
// uploadImage — POST create upload destination + PUT image bytes
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonAplusApi::uploadImage(QString marketplaceId,
                                               QByteArray imageBytes,
                                               QString contentType,
                                               QString *out)
{
    out->clear();

    // Step 1 — POST /uploads/2020-11-01/uploadDestinations/{resource}
    const QByteArray md5Bytes = QCryptographicHash::hash(imageBytes, QCryptographicHash::Md5);
    const QString contentMd5  = QString::fromLatin1(md5Bytes.toBase64());

    // The Uploads API is only available on the NA endpoint (sellingpartnerapi-na).
    // Upload destination IDs are S3-backed and cross-region usable, so we always
    // upload via NA regardless of which marketplace the A+ content targets.
    const QString uploadEndpoint    = QStringLiteral("sellingpartnerapi-na.amazon.com");
    // NA marketplace ID used only for the upload call; content is still published
    // to the original marketplaceId.
    const QString uploadMarketplace = QStringLiteral("ATVPDKIKX0DER");

    const QString resource = QStringLiteral("aplus/2020-11-01/contentDocuments");
    const QString encodedResource = QString::fromUtf8(QUrl::toPercentEncoding(resource));

    QUrl uploadsUrl = QUrl::fromEncoded(
        QStringLiteral("https://%1/uploads/2020-11-01/uploadDestinations/%2")
            .arg(uploadEndpoint, encodedResource).toUtf8());
    QUrlQuery uploadsQuery;
    uploadsQuery.addQueryItem(QStringLiteral("marketplaceIds"), uploadMarketplace);
    uploadsQuery.addQueryItem(QStringLiteral("contentMD5"),     contentMd5);
    uploadsQuery.addQueryItem(QStringLiteral("contentType"),    contentType);
    uploadsUrl.setQuery(uploadsQuery);

    QString token;
    co_await _getAccessToken(QStringLiteral("NA"), &token);
    if (token.isEmpty()) {
        m_lastError = QStringLiteral("No access token for marketplace %1").arg(marketplaceId);
        co_return;
    }

    const QByteArray uploadsBodyBytes;  // no request body — all params are in the query string

    QNetworkRequest uploadsReq(uploadsUrl);
    uploadsReq.setRawHeader("x-amz-access-token", token.toUtf8());
    uploadsReq.setRawHeader("accept", "application/json");

    qDebug() << "AmazonAplusApi: POST upload destination" << uploadsUrl.toString();
    QNetworkReply *uploadsReply = _nam()->post(uploadsReq, uploadsBodyBytes);
    co_await qCoro(uploadsReply).waitForFinished();

    const QByteArray uploadsData = uploadsReply->readAll();
    const int uploadsStatus = uploadsReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString uploadsRequestId = rawHeaderCI(uploadsReply, "x-amzn-RequestId");
    uploadsReply->deleteLater();

    qDebug() << "AmazonAplusApi: upload destination HTTP" << uploadsStatus
             << "RequestId:" << uploadsRequestId
             << "response:" << QString::fromUtf8(uploadsData.left(300));

    if (uploadsStatus != 201 && uploadsStatus != 200) {
        m_lastError = QStringLiteral("Upload destination HTTP %1: %2")
                          .arg(uploadsStatus).arg(QString::fromUtf8(uploadsData.left(300)));
        writeAplusDiagnostic(QStringLiteral("upload"),
                             QStringLiteral("POST ") + uploadsUrl.toString(),
                             uploadsBodyBytes, uploadsStatus, uploadsRequestId, uploadsData);
        co_return;
    }

    const QJsonObject uploadsDoc = QJsonDocument::fromJson(uploadsData).object();
    const QJsonObject payload    = uploadsDoc.value(QStringLiteral("payload")).toObject();
    const QString uploadDestinationId = payload.value(QStringLiteral("uploadDestinationId")).toString();
    const QString presignedUrl        = payload.value(QStringLiteral("url")).toString();
    const QJsonObject extraHeaders    = payload.value(QStringLiteral("headers")).toObject();

    if (uploadDestinationId.isEmpty() || presignedUrl.isEmpty()) {
        m_lastError = QStringLiteral("Upload destination missing fields: %1")
                          .arg(QString::fromUtf8(uploadsData.left(300)));
        writeAplusDiagnostic(QStringLiteral("upload"),
                             QStringLiteral("POST ") + uploadsUrl.toString(),
                             uploadsBodyBytes, uploadsStatus, uploadsRequestId, uploadsData);
        co_return;
    }

    // Step 2 — POST image to S3 as multipart/form-data.
    // A+ content uses S3 pre-signed POST (not PUT). The `headers` field in the
    // Uploads API response contains the required S3 form fields (acl, key,
    // policy, x-amz-credential, x-amz-date, x-amz-signature, …). The image
    // itself goes as the last "File" part per Amazon's documentation.
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    for (auto it = extraHeaders.constBegin(); it != extraHeaders.constEnd(); ++it) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(it.key()));
        part.setBody(it.value().toString().toUtf8());
        multipart->append(part);
    }

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"File\""));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    filePart.setBody(imageBytes);
    multipart->append(filePart);

    qDebug() << "AmazonAplusApi: POST (multipart)" << imageBytes.size()
             << "bytes to" << presignedUrl.left(60);
    QNetworkRequest postReq{QUrl(presignedUrl)};
    QNetworkReply *putReply = _nam()->post(postReq, multipart);
    multipart->setParent(putReply); // deleted with the reply
    co_await qCoro(putReply).waitForFinished();

    const QByteArray putData  = putReply->readAll();
    const int        putStatus = putReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError putError = putReply->error();
    putReply->deleteLater();

    qDebug() << "AmazonAplusApi: S3 POST HTTP" << putStatus;

    // S3 pre-signed POST returns 204 No Content on success (no body).
    if (putStatus != 200 && putStatus != 204 && putError != QNetworkReply::NoError) {
        m_lastError = QStringLiteral("S3 POST image HTTP %1: %2")
                          .arg(putStatus).arg(QString::fromUtf8(putData.left(300)));
        writeAplusDiagnostic(QStringLiteral("upload-s3post"),
                             QStringLiteral("POST ") + presignedUrl,
                             QByteArray{}, putStatus, QString{}, putData);
        co_return;
    }

    *out = uploadDestinationId;
    co_return;
}

// ---------------------------------------------------------------------------
// createContentDocument — POST /aplus/2020-11-01/contentDocuments
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonAplusApi::createContentDocument(QString marketplaceId,
                                                          QJsonObject contentDocument,
                                                          QString *out)
{
    out->clear();

    const QString endpoint = endpointForMarketplace(marketplaceId);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(QStringLiteral("/aplus/2020-11-01/contentDocuments"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceId"), marketplaceId);
    url.setQuery(query);

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) {
        m_lastError = QStringLiteral("No access token for marketplace %1").arg(marketplaceId);
        co_return;
    }

    const QJsonObject body{
        {QStringLiteral("contentDocument"), contentDocument}
    };
    const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonAplusApi: POST createContentDocument" << url.toString()
             << "body bytes:" << bodyBytes.size();
    QNetworkReply *reply = _nam()->post(req, bodyBytes);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(reply, "x-amzn-RequestId");
    reply->deleteLater();

    qDebug() << "AmazonAplusApi: createContentDocument HTTP" << status
             << "RequestId:" << requestId
             << "response:" << QString::fromUtf8(data.left(300));

    if (status != 200 && status != 201) {
        m_lastError = QStringLiteral("createContentDocument HTTP %1: %2")
                          .arg(status).arg(QString::fromUtf8(data.left(400)));
        writeAplusDiagnostic(QStringLiteral("create"),
                             QStringLiteral("POST ") + url.toString(),
                             bodyBytes, status, requestId, data);
        co_return;
    }

    const QJsonObject root = QJsonDocument::fromJson(data).object();
    *out = root.value(QStringLiteral("contentReferenceKey")).toString();
    if (out->isEmpty()) {
        m_lastError = QStringLiteral("createContentDocument response missing contentReferenceKey: %1")
                          .arg(QString::fromUtf8(data.left(300)));
        writeAplusDiagnostic(QStringLiteral("create"),
                             QStringLiteral("POST ") + url.toString(),
                             bodyBytes, status, requestId, data);
    }
    co_return;
}

// ---------------------------------------------------------------------------
// postAsinRelations — POST /aplus/2020-11-01/contentDocuments/{key}/asins
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonAplusApi::postAsinRelations(QString contentReferenceKey,
                                                     QString marketplaceId,
                                                     QStringList asins,
                                                     bool *success)
{
    *success = false;

    const QString endpoint = endpointForMarketplace(marketplaceId);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(QStringLiteral("/aplus/2020-11-01/contentDocuments/")
                + contentReferenceKey + QStringLiteral("/asins"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceId"), marketplaceId);
    url.setQuery(query);

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) {
        m_lastError = QStringLiteral("No access token for marketplace %1").arg(marketplaceId);
        co_return;
    }

    QJsonArray asinSet;
    for (const QString &a : asins)
        asinSet.append(a);
    const QJsonObject bodyObj{
        {QStringLiteral("asinSet"), asinSet}
    };
    const QByteArray bodyBytes = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonAplusApi: POST asinRelations" << url.toString()
             << "asins:" << asins;
    QNetworkReply *reply = _nam()->post(req, bodyBytes);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(reply, "x-amzn-RequestId");
    reply->deleteLater();

    qDebug() << "AmazonAplusApi: postAsinRelations HTTP" << status
             << "RequestId:" << requestId
             << "response:" << QString::fromUtf8(data.left(300));

    if (status != 200 && status != 201) {
        m_lastError = QStringLiteral("postAsinRelations HTTP %1: %2")
                          .arg(status).arg(QString::fromUtf8(data.left(400)));
        writeAplusDiagnostic(QStringLiteral("asin"),
                             QStringLiteral("POST ") + url.toString(),
                             bodyBytes, status, requestId, data);
        co_return;
    }

    *success = true;
    co_return;
}

// ---------------------------------------------------------------------------
// validateContentDocumentAsinRelations
//   POST /aplus/2020-11-01/contentDocuments/{key}/asins/validation
// HTTP 200 means the request was processed — NOT that validation passed.
// Always inspect errors/warnings in the response body.
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonAplusApi::validateContentDocumentAsinRelations(
    QString contentReferenceKey,
    QString marketplaceId,
    QJsonObject contentDocument,
    QStringList asins,
    QStringList *errors,
    QStringList *warnings)
{
    errors->clear();
    warnings->clear();

    const QString endpoint = endpointForMarketplace(marketplaceId);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(QStringLiteral("/aplus/2020-11-01/contentDocuments/")
                + contentReferenceKey + QStringLiteral("/asins/validation"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceId"), marketplaceId);
    for (const QString &asin : asins)
        query.addQueryItem(QStringLiteral("asinSet"), asin);
    url.setQuery(query);

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) {
        errors->append(QStringLiteral("No access token for marketplace %1").arg(marketplaceId));
        co_return;
    }

    const QJsonObject bodyObj{
        {QStringLiteral("contentDocument"), contentDocument}
    };
    const QByteArray bodyBytes = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonAplusApi: POST validate asins" << url.toString()
             << "asins:" << asins;
    QNetworkReply *reply = _nam()->post(req, bodyBytes);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(reply, "x-amzn-RequestId");
    reply->deleteLater();

    qDebug() << "AmazonAplusApi: validateContentDocumentAsinRelations HTTP" << status
             << "RequestId:" << requestId
             << "response:" << QString::fromUtf8(data.left(500));

    if (status != 200) {
        errors->append(QStringLiteral("HTTP %1: %2")
                           .arg(status).arg(QString::fromUtf8(data.left(400))));
        writeAplusDiagnostic(QStringLiteral("validate"),
                             QStringLiteral("POST ") + url.toString(),
                             bodyBytes, status, requestId, data);
        co_return;
    }

    const QJsonObject root = QJsonDocument::fromJson(data).object();

    const QJsonArray errArr = root.value(QStringLiteral("errors")).toArray();
    for (const QJsonValue &v : errArr) {
        const QJsonObject e = v.toObject();
        const QString code = e.value(QStringLiteral("code")).toString();
        const QString msg  = e.value(QStringLiteral("message")).toString();
        errors->append(code.isEmpty() ? msg : QStringLiteral("[%1] %2").arg(code, msg));
    }

    const QJsonArray warnArr = root.value(QStringLiteral("warnings")).toArray();
    for (const QJsonValue &v : warnArr) {
        const QJsonObject w = v.toObject();
        const QString code = w.value(QStringLiteral("code")).toString();
        const QString msg  = w.value(QStringLiteral("message")).toString();
        warnings->append(code.isEmpty() ? msg : QStringLiteral("[%1] %2").arg(code, msg));
    }

    if (!errors->isEmpty())
        writeAplusDiagnostic(QStringLiteral("validate"),
                             QStringLiteral("POST ") + url.toString(),
                             bodyBytes, status, requestId, data);
    co_return;
}

// ---------------------------------------------------------------------------
// submitForApproval — POST /aplus/.../approvalSubmissions
// ---------------------------------------------------------------------------

QCoro::Task<void> AmazonAplusApi::submitForApproval(QString contentReferenceKey,
                                                     QString marketplaceId,
                                                     bool *success)
{
    *success = false;

    const QString endpoint = endpointForMarketplace(marketplaceId);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(endpoint);
    url.setPath(QStringLiteral("/aplus/2020-11-01/contentDocuments/")
                + contentReferenceKey + QStringLiteral("/approvalSubmissions"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("marketplaceId"), marketplaceId);
    url.setQuery(query);

    QString token;
    co_await _getAccessToken(lwaRegionForMarketplace(marketplaceId), &token);
    if (token.isEmpty()) {
        m_lastError = QStringLiteral("No access token for marketplace %1").arg(marketplaceId);
        co_return;
    }

    const QByteArray bodyBytes = QByteArrayLiteral("{}");

    QNetworkRequest req(url);
    req.setRawHeader("x-amz-access-token", token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("accept", "application/json");

    qDebug() << "AmazonAplusApi: POST approvalSubmissions" << url.toString();
    QNetworkReply *reply = _nam()->post(req, bodyBytes);
    co_await qCoro(reply).waitForFinished();

    const QByteArray data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString requestId = rawHeaderCI(reply, "x-amzn-RequestId");
    reply->deleteLater();

    qDebug() << "AmazonAplusApi: submitForApproval HTTP" << status
             << "RequestId:" << requestId
             << "response:" << QString::fromUtf8(data.left(300));

    if (status != 200 && status != 201) {
        m_lastError = QStringLiteral("submitForApproval HTTP %1: %2")
                          .arg(status).arg(QString::fromUtf8(data.left(400)));
        writeAplusDiagnostic(QStringLiteral("approval"),
                             QStringLiteral("POST ") + url.toString(),
                             bodyBytes, status, requestId, data);
        co_return;
    }

    *success = true;
    co_return;
}
