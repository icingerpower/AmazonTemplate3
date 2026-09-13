#include "TableReviews.h"

#include <algorithm>
#include <QSize>

TableReviews::TableReviews(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int TableReviews::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_reviews.size();
}

int TableReviews::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant TableReviews::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColImage:       return tr("Image");
    case ColCountry:     return tr("Country");
    case ColStars:       return tr("Stars");
    case ColLink:        return tr("Link");
    case ColAsin:        return tr("ASIN");
    case ColReview:      return tr("Review (Title + Text)");
    case ColTranslation: return tr("Translation (EN)");
    case ColDate:        return tr("Date");
    default:             return {};
    }
}

Qt::ItemFlags TableReviews::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColReview || index.column() == ColTranslation || index.column() == ColAsin)
        f |= Qt::ItemIsEditable; // Read in full / copy-paste; setData is not implemented
    return f;
}

QVariant TableReviews::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_reviews.size() || index.row() < 0)
        return {};

    const ReviewItem &r = m_reviews.at(index.row());
    const int col = index.column();

    if (role == Qt::SizeHintRole && col == ColImage)
        return QSize(58, 58);

    if (col == ColImage) {
        if (role == Qt::DecorationRole) {
            auto it = m_images.constFind(r.id);
            if (it != m_images.constEnd() && !it.value().isNull())
                return it.value();
            if (!r.asin.isEmpty()) {
                it = m_images.constFind(r.asin);
                if (it != m_images.constEnd() && !it.value().isNull())
                    return it.value();
            }
            return {};
        }
        return {};
    }

    if (role == Qt::TextAlignmentRole) {
        if (col == ColCountry || col == ColStars || col == ColDate || col == ColAsin)
            return QVariant(Qt::AlignCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::ToolTipRole) {
        if (col == ColReview) {
            QString tip;
            if (!r.productTitle.isEmpty())
                tip += tr("Product: %1\nASIN: %2\n\n").arg(r.productTitle, r.asin);
            tip += r.text;
            return tip;
        }
        if (col == ColLink) return r.link;
        if (col == ColAsin) return r.productTitle.isEmpty() ? r.asin : (r.asin + QStringLiteral(" - ") + r.productTitle);
        if (col == ColTranslation) return r.translation;
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
        case ColCountry:
            return r.country;
        case ColStars: {
            QString stars;
            int s = std::clamp(r.stars, 0, 5);
            for (int i = 0; i < s; ++i) stars += QString::fromUtf8("\xE2\x98\x85");
            for (int i = s; i < 5; ++i) stars += QString::fromUtf8("\xE2\x98\x86");
            return QStringLiteral("%1 (%2)").arg(stars).arg(s);
        }
        case ColLink:
            return r.link.isEmpty() ? QVariant{} : r.link;
        case ColAsin:
            return r.asin;
        case ColReview: {
            if (!r.title.isEmpty() && !r.text.isEmpty())
                return r.title + QStringLiteral("\n\n") + r.text;
            if (!r.title.isEmpty())
                return r.title;
            return r.text;
        }
        case ColTranslation:
            return r.translation;
        case ColDate:
            return r.date;
        default:
            return {};
        }
    }

    return {};
}

const ReviewItem &TableReviews::reviewAt(int row) const
{
    Q_ASSERT(row >= 0 && row < m_reviews.size());
    return m_reviews.at(row);
}

void TableReviews::_sortInternal(int column, Qt::SortOrder order)
{
    std::stable_sort(m_reviews.begin(), m_reviews.end(), [column, order](const ReviewItem &a, const ReviewItem &b) {
        if (column == ColDate) {
            const QDate da = a.parsedDate();
            const QDate db = b.parsedDate();
            if (da.isValid() && db.isValid()) {
                if (da != db)
                    return (order == Qt::AscendingOrder) ? (da < db) : (da > db);
            } else if (da.isValid() != db.isValid()) {
                return (order == Qt::AscendingOrder) ? (!da.isValid()) : (da.isValid());
            }
            return (order == Qt::AscendingOrder) ? (a.date < b.date) : (a.date > b.date);
        }
        if (column == ColStars) {
            return (order == Qt::AscendingOrder) ? (a.stars < b.stars) : (a.stars > b.stars);
        }
        if (column == ColCountry) {
            return (order == Qt::AscendingOrder) ? (a.country < b.country) : (a.country > b.country);
        }
        if (column == ColReview) {
            return (order == Qt::AscendingOrder) ? (a.text < b.text) : (a.text > b.text);
        }
        if (column == ColTranslation) {
            return (order == Qt::AscendingOrder) ? (a.translation < b.translation) : (a.translation > b.translation);
        }
        if (column == ColLink) {
            return (order == Qt::AscendingOrder) ? (a.link < b.link) : (a.link > b.link);
        }
        if (column == ColAsin) {
            return (order == Qt::AscendingOrder) ? (a.asin < b.asin) : (a.asin > b.asin);
        }
        return false;
    });
}

void TableReviews::sort(int column, Qt::SortOrder order)
{
    layoutAboutToBeChanged();
    _sortInternal(column, order);
    layoutChanged();
}

void TableReviews::setReviews(const QList<ReviewItem> &reviews)
{
    beginResetModel();
    m_reviews = reviews;
    _sortInternal(ColDate, Qt::DescendingOrder);
    endResetModel();
}

bool TableReviews::addOrUpdateReview(const ReviewItem &review)
{
    for (int i = 0; i < m_reviews.size(); ++i) {
        if (m_reviews[i].id == review.id && !review.id.isEmpty()) {
            const QString existingTranslation = m_reviews[i].translation;
            const bool existingChecked = m_reviews[i].translationChecked;
            m_reviews[i] = review;
            if (m_reviews[i].translation.isEmpty()) {
                m_reviews[i].translation = existingTranslation;
                m_reviews[i].translationChecked = existingChecked;
            }
            emit dataChanged(index(i, 0), index(i, ColumnCount - 1));
            return false;
        }
    }

    // Insert in descending date order (most recent first)
    const QDate newDate = review.parsedDate();
    int insertIdx = m_reviews.size();
    for (int i = 0; i < m_reviews.size(); ++i) {
        const QDate curDate = m_reviews[i].parsedDate();
        if (newDate.isValid() && curDate.isValid()) {
            if (newDate > curDate) {
                insertIdx = i;
                break;
            }
        } else if (newDate.isValid() && !curDate.isValid()) {
            insertIdx = i;
            break;
        } else if (!newDate.isValid() && !curDate.isValid()) {
            if (review.date > m_reviews[i].date) {
                insertIdx = i;
                break;
            }
        }
    }

    beginInsertRows({}, insertIdx, insertIdx);
    m_reviews.insert(insertIdx, review);
    endInsertRows();
    return true;
}

void TableReviews::updateTranslation(int row, const QString &translation)
{
    if (row < 0 || row >= m_reviews.size()) return;
    m_reviews[row].translation = translation;
    m_reviews[row].translationChecked = true;
    const QModelIndex idx = index(row, ColTranslation);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void TableReviews::updateImage(const QString &idOrAsin, const QPixmap &pixmap)
{
    if (idOrAsin.isEmpty()) return;
    m_images.insert(idOrAsin, pixmap);

    for (int i = 0; i < m_reviews.size(); ++i) {
        if (m_reviews[i].id == idOrAsin || m_reviews[i].asin == idOrAsin) {
            const QModelIndex idx = index(i, ColImage);
            emit dataChanged(idx, idx, {Qt::DecorationRole});
        }
    }
}

void TableReviews::removeAt(int row)
{
    if (row < 0 || row >= m_reviews.size()) return;
    beginRemoveRows({}, row, row);
    m_reviews.removeAt(row);
    endRemoveRows();
}

void TableReviews::clear()
{
    beginResetModel();
    m_reviews.clear();
    m_images.clear();
    endResetModel();
}
