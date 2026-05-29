#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QCoro/QCoroTask>

class QNetworkAccessManager;

class AmazonAplusApi : public QObject
{
    Q_OBJECT
public:
    explicit AmazonAplusApi(
        const QString &clientId,
        const QString &clientSecret,
        const QString &euRefreshToken,
        const QString &naRefreshToken,
        const QString &jpRefreshToken,
        QObject *parent = nullptr);
    ~AmazonAplusApi() override;

    QString lastError() const { return m_lastError; }
    void    clearLastError()  { m_lastError.clear(); }

    // Step 1: Upload one image to the SP-API Uploads endpoint for A+ content.
    // Writes uploadDestinationId into *out on success, empty string on failure.
    // contentType: "image/png" or "image/jpeg"
    // GCC 13 ICE workaround: non-trivially-destructible params passed by value.
    QCoro::Task<void> uploadImage(QString marketplaceId,
                                   QByteArray imageBytes,
                                   QString contentType,
                                   QString *out);

    // Step 2: Create A+ content document. contentDocument is the inner object
    // {name, contentType:"EMC", locale, contentModuleList:[...]}.
    // Writes contentReferenceKey into *out on success.
    QCoro::Task<void> createContentDocument(QString marketplaceId,
                                             QJsonObject contentDocument,
                                             QString *out);

    // Step 3: Associate content with ASINs.
    QCoro::Task<void> postAsinRelations(QString contentReferenceKey,
                                         QString marketplaceId,
                                         QStringList asins,
                                         bool *success);

    // Step 3b: Validate content document against ASINs (run after postAsinRelations).
    // HTTP 200 does NOT mean validation passed — always check *errors on return.
    QCoro::Task<void> validateContentDocumentAsinRelations(
                          QString contentReferenceKey,
                          QString marketplaceId,
                          QJsonObject contentDocument,
                          QStringList asins,
                          QStringList *errors,
                          QStringList *warnings);

    // Step 4: Submit for Amazon approval/publication.
    QCoro::Task<void> submitForApproval(QString contentReferenceKey,
                                         QString marketplaceId,
                                         bool *success);

    // Returns the SP-API regional endpoint host for a marketplace ID.
    static QString endpointForMarketplace(const QString &marketplaceId);

    // Returns the default A+ content locale string for a marketplace ID.
    // E.g. "A1F83G8C2ARO7P" → "en_GB", "A1PA6795UKMFR9" → "de_DE".
    static QString localeForMarketplace(const QString &marketplaceId);

private:
    // LWA token refresh (output param — GCC 13 ICE workaround).
    QCoro::Task<void> _getAccessToken(QString lwaRegion, QString *out);
    static QString    lwaRegionForMarketplace(const QString &marketplaceId);
    QNetworkAccessManager *_nam();

    QString m_clientId, m_clientSecret;
    QString m_euRefresh, m_naRefresh, m_jpRefresh;

    QString   m_tokenEu; QDateTime m_expiryEu;
    QString   m_tokenNa; QDateTime m_expiryNa;
    QString   m_tokenJp; QDateTime m_expiryJp;

    QNetworkAccessManager *m_nam = nullptr;
    QString m_lastError;
};
