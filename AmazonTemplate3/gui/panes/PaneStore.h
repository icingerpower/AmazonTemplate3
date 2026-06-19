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
#include "AbstractCli.h"
#include "../DialogGenStorefrontImage.h"

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
    void setAvailableClis(const QList<AbstractCli *> &clis);

private:
    Ui::PaneStore          *ui;
    QDir                    m_workingDir;
    AmazonCatalogApi       *m_catalogApi     = nullptr;
    TreeBrandCategories    *m_treeModel      = nullptr;
    QStandardItemModel     *m_countriesModel = nullptr;
    TableStoreAsin         *m_storeModel     = nullptr;

    QList<AbstractCli *>    m_availableClis;

    QHash<QString, AmazonCatalogApi::StoreItem> m_asinToItem;
    QHash<QString, QPixmap>                     m_asinToPixmap;
    QList<AmazonCatalogApi::StoreItem>          m_items;

    // User-defined display order: list of representative ASINs in the order the user arranged.
    // New products (not in this list) appear first; known ones appear in order.
    QStringList             m_savedOrder;
    QList<QStringList>      m_customPaths; // manually added tree node paths, persisted

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
    void _onMoveProducts();
    void _onRemoveProducts();
    void _onAddCategory();
    void _onRemoveCategory();
    void _onCopyAsins();

    bool            _isCurrentNodeCustom() const;
    void            _loadCustomPaths();
    void            _saveCustomPaths();
    void _onMoveToTop();
    void _onMoveUp();
    void _onMoveDown();
    void _onMoveToBottom();
    void _loadOrder();
    void _saveOrder();

    QStringList _currentNodePath() const;

    void _onGenStorefrontImage();
    void _loadStorefrontVersions();           // refreshes listVersionStrip + image labels
    void _onStorefrontImageGenerated(const QString &desktopPath, const QString &mobilePath);
    void _showStorefrontImage(const QString &absPath);
    void _deleteSelectedVersion();

    QCoro::Task<void> _onRetrieve();
    QCoro::Task<void> _loadImages(QStringList asins);
};

#endif // PANESTORE_H
