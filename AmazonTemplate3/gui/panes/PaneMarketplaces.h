#ifndef PANEMARKETPLACES_H
#define PANEMARKETPLACES_H

#include <QList>
#include <QPointer>
#include <QWidget>
#include <QCoro/QCoroTask>

QT_BEGIN_NAMESPACE
namespace Ui { class PaneMarketplaces; }
QT_END_NAMESPACE

class AbstractInventorySource;
class AbstractTargetMarketplace;
class TableMarketplaceProducts;
class TableMarketplaceOrders;
class QDialog;

class PaneMarketplaces : public QWidget
{
    Q_OBJECT
public:
    explicit PaneMarketplaces(QWidget *parent = nullptr);
    ~PaneMarketplaces();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    Ui::PaneMarketplaces     *ui;
    TableMarketplaceProducts *m_model = nullptr;
    TableMarketplaceOrders   *m_ordersModel = nullptr;

    // Configured platforms, built by the registered factories (Recorder
    // pattern) from the working directory settings. Owned. Rebuilt on Load.
    QList<AbstractTargetMarketplace *> m_marketplaces;
    QList<AbstractInventorySource *>   m_sources;

    QCoro::Task<void>  m_loadTask;
    QCoro::Task<void>  m_loadOrdersTask;
    QCoro::Task<void>  m_syncOrdersTask;
    QCoro::Task<void>  m_syncInventoryTask;
    QCoro::Task<void>  m_shipByAmzTask;
    QPointer<QDialog>  m_progressDlg; // survives tab switches; null when not loading

    void _rebuildPlatforms();
    AbstractInventorySource   *_source() const; // first configured source, or nullptr
    AbstractTargetMarketplace *_marketplaceById(const QString &id) const;
    void                _invalidateAmazonCacheForSku(const QString &sku);
    QCoro::Task<void>   _onLoad();
    QCoro::Task<void>   _onLoadOrders();
    QCoro::Task<void>   _onSyncOrders();
    QCoro::Task<void>   _onSyncInventory();
    QCoro::Task<void>   _onShipByAmazon();
};

#endif // PANEMARKETPLACES_H
