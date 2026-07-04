#ifndef PANEMARKETPLACES_H
#define PANEMARKETPLACES_H

#include <QPointer>
#include <QWidget>
#include <QCoro/QCoroTask>

QT_BEGIN_NAMESPACE
namespace Ui { class PaneMarketplaces; }
QT_END_NAMESPACE

class AmazonInventoryApi;
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
    AmazonInventoryApi       *m_api   = nullptr;
    TableMarketplaceProducts *m_model = nullptr;
    TableMarketplaceOrders   *m_ordersModel = nullptr;

    QCoro::Task<void>  m_loadTask;
    QCoro::Task<void>  m_loadOrdersTask;
    QCoro::Task<void>  m_syncOrdersTask;
    QCoro::Task<void>  m_syncInventoryTask;
    QCoro::Task<void>  m_shipByAmzTask;
    QPointer<QDialog>  m_progressDlg; // survives tab switches; null when not loading

    AmazonInventoryApi *_api();
    void                _invalidateAmazonCacheForSku(const QString &sku);
    QCoro::Task<void>   _onLoad();
    QCoro::Task<void>   _onLoadOrders();
    QCoro::Task<void>   _onSyncOrders();
    QCoro::Task<void>   _onSyncInventory();
    QCoro::Task<void>   _onShipByAmazon();
};

#endif // PANEMARKETPLACES_H
