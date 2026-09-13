#ifndef TABLEREVIEWS_H
#define TABLEREVIEWS_H

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QString>

#include "CaseWorkerRunner.h"

class TableReviews : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        ColImage = 0,
        ColCountry,
        ColStars,
        ColLink,
        ColAsin,
        ColReview,
        ColTranslation,
        ColDate,
        ColumnCount
    };

    explicit TableReviews(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void setReviews(const QList<ReviewItem> &reviews);
    bool addOrUpdateReview(const ReviewItem &review);
    const QList<ReviewItem> &reviews() const { return m_reviews; }
    const ReviewItem &reviewAt(int row) const;
    void updateTranslation(int row, const QString &translation);
    void updateImage(const QString &idOrAsin, const QPixmap &pixmap);
    void removeAt(int row);
    void clear();

private:
    void _sortInternal(int column, Qt::SortOrder order);

    QList<ReviewItem>     m_reviews;
    QHash<QString, QPixmap> m_images; // Keyed by review.id or review.asin
};

#endif // TABLEREVIEWS_H
