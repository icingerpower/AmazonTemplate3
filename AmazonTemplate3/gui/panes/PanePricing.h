#ifndef PANEPRICING_H
#define PANEPRICING_H

#include <QList>
#include <QPointer>
#include <QWidget>
#include <QCoro/QCoroTask>

QT_BEGIN_NAMESPACE
namespace Ui { class PanePricing; }
QT_END_NAMESPACE

class AbstractCli;
class QDialog;
class TableCurrencyRates;
class TablePricing;

class PanePricing : public QWidget
{
    Q_OBJECT
public:
    explicit PanePricing(QWidget *parent = nullptr);
    ~PanePricing();

    void setAvailableClis(const QList<AbstractCli *> &clis);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    Ui::PanePricing      *ui;
    TableCurrencyRates   *m_ratesModel    = nullptr;
    TablePricing         *m_pricingModel  = nullptr;
    QList<AbstractCli *>  m_availableClis;
    QPointer<QDialog>     m_progressDlg;  // survives tab switches; null when idle
    QCoro::Task<void>     m_refreshTask;
    QCoro::Task<void>     m_retrieveTask;
    QCoro::Task<void>     m_updateTask;

    QCoro::Task<void> _onRefreshRate();
    QCoro::Task<void> _onRetrieve();
    QCoro::Task<void> _onUpdate();
};

#endif // PANEPRICING_H
