#pragma once
#include "PricingData.h"
#include <QDir>

// Shared, account-scoped observations. UI proposals and filters never live here.
class AmazonDataCache {
  public:
    AmazonDataCache(QDir workingDir, QString accountIdentity);
    QJsonObject read(const QString &kind, const QString &key) const;
    QList<QJsonObject> all(const QString &kind) const;
    bool write(const QString &kind, const QString &key, const QJsonObject &payload, qint64 fetchedUtc,
               QString *error = nullptr) const;
    bool invalidate(const QString &kind, const QString &key, QString *error = nullptr) const;
    static bool fresh(const QJsonObject &record, qint64 ttlSeconds, qint64 now);
    static QString listingKey(const QString &marketplace, const QString &sku);
    // Read-only import of exported and still-active legacy cache files.
    QList<Pricing::Row> seedRows(const QList<Pricing::Market> &markets) const;
    QDir root() const {
        return m_root;
    }

  private:
    QDir m_working, m_root;
    QString path(const QString &kind, const QString &key) const;
};
