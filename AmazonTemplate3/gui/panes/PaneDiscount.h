#ifndef PANEDISCOUNT_H
#define PANEDISCOUNT_H

#include <QPointer>
#include <QWidget>
#include <QCoro/QCoroTask>

QT_BEGIN_NAMESPACE
namespace Ui { class PaneDiscount; }
QT_END_NAMESPACE

class QDialog;
class QStandardItemModel;
class TreeSkuDiscount;

class PaneDiscount : public QWidget
{
    Q_OBJECT
public:
    explicit PaneDiscount(QWidget *parent = nullptr);
    ~PaneDiscount();

private:
    Ui::PaneDiscount   *ui;
    TreeSkuDiscount    *m_model          = nullptr;
    QStandardItemModel *m_countriesModel = nullptr;
    QPointer<QDialog>   m_progressDlg;   // survives tab switches; null when idle
    QCoro::Task<void>   m_loadTask;
    QCoro::Task<void>   m_applyTask;

    void _buildCountriesModel();

    QCoro::Task<void> _onLoad();
    QCoro::Task<void> _onApply();
};

#endif // PANEDISCOUNT_H
