#pragma once

#include "apis/AmazonCatalogApi.h"
#include <QDir>
#include <QJsonObject>

// A complete regional order report covers every ASIN and SKU. Cache successful
// 30-day slices independently so retrying a failed slice never adds it twice.
class StoreSales
{
public:
    struct Total {
        int units = -1;
        bool complete = false;
    };
    void load(const QDir &directory, const QString &accountScope);
    Total total(const QStringList &asins) const;
    bool fresh() const;
    bool refreshSuggested() const;
    QDateTime endTime() const { return m_end; }
    QString error() const { return m_error; }
    QCoro::Task<void> refresh(AmazonCatalogApi *api, QList<QStringList> regions,
                             std::shared_ptr<bool> cancelled,
                             std::function<void(const QString &)> progress,
                             std::function<void(int completed, int total)> progressCounts = {},
                             bool forceRefresh = false);
private:
    bool save();
    QDir m_directory;
    QString m_scope, m_error;
    QDateTime m_end;
    QJsonObject m_slices;
    QStringList m_expected;
};
