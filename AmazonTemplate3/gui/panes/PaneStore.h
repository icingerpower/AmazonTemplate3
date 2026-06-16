#ifndef PANESTORE_H
#define PANESTORE_H

#include <QDir>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include <QCoro/QCoroTask>

#include "apis/AmazonCatalogApi.h"
#include "AmazonMarketplace.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneStore; }
QT_END_NAMESPACE

class TableStoreAsin;
class TreeBrandCategories;
class QStandardItemModel;

class PaneStore : public QWidget
{
    Q_OBJECT

public:
    explicit PaneStore(QWidget *parent = nullptr);
    ~PaneStore();

    void setWorkingDir(const QDir &workingDir);

private:
    Ui::PaneStore          *ui;
    QDir                    m_workingDir;
    AmazonCatalogApi       *m_catalogApi     = nullptr;
    TreeBrandCategories    *m_treeModel      = nullptr;
    QStandardItemModel     *m_countriesModel = nullptr;
    TableStoreAsin         *m_storeModel     = nullptr;

    QHash<QString, AmazonCatalogApi::StoreItem> m_asinToItem;
    QHash<QString, QPixmap>                     m_asinToPixmap;
    QList<AmazonCatalogApi::StoreItem>          m_items;

    // User-defined display order: list of representative ASINs in the order the user arranged.
    // New products (not in this list) appear first; known ones appear in order.
    QStringList m_savedOrder;

    QCoro::Task<void> m_retrieveTask;
    QCoro::Task<void> m_imageTask;

    AmazonCatalogApi *_catalogApi();
    QString           _marketplaceId() const;

    bool eventFilter(QObject *obj, QEvent *event) override;

    void _populateCountriesList();
    void _adjustCountriesHeight();
    void _loadFromDisk(const QString &marketplaceId);
    void _saveToDisk(const QString &marketplaceId,
                     const QList<AmazonCatalogApi::StoreItem> &items);
    void _applyItems(const QList<AmazonCatalogApi::StoreItem> &items);
    void _buildTable(const QStringList &asins);
    void _onTreeSelectionChanged();
    void _onCountrySelectionChanged();
    void _updateTableForCurrentSelection();
    void _onMerge();
    void _onCopyAsins();
    void _onMoveUp();
    void _onMoveDown();
    void _loadOrder();
    void _saveOrder();

    QCoro::Task<void> _onRetrieve();
    QCoro::Task<void> _loadImages(QStringList asins);
};

#endif // PANESTORE_H
