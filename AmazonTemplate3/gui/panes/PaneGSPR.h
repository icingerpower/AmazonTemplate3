#ifndef PANE_GSPR_H
#define PANE_GSPR_H

#include <QDialog>
#include <QDir>
#include <QList>
#include <QPointer>
#include <QWidget>

#include <QCoro/QCoroTask>

#include "AbstractCli.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneGSPR; }
QT_END_NAMESPACE

class AmazonWarningsApi;
class TableGpsrManufacturers;
class TableWarningsManufacturer;

class PaneGSPR : public QWidget
{
    Q_OBJECT

public:
    explicit PaneGSPR(QWidget *parent = nullptr);
    ~PaneGSPR();
    void setWorkingDir(const QDir &workingDir);
    void setAvailableClis(const QList<AbstractCli *> &clis);

private:
    Ui::PaneGSPR              *ui;
    QDir                       m_workingDir;
    QList<AbstractCli *>       m_availableClis;
    AmazonWarningsApi         *m_api                  = nullptr;
    TableGpsrManufacturers    *m_manufacturers        = nullptr;
    TableWarningsManufacturer *m_warningsManufacturer = nullptr;

    QCoro::Task<void> m_loadAllTask;
    QCoro::Task<void> m_loadManufacturersTask;

    QPointer<QDialog> m_progressDlg;

    AmazonWarningsApi *_api();
    QCoro::Task<void>  _onLoadManufacturers();
    QCoro::Task<void>  _onLoadAll();
};

#endif // PANE_GSPR_H
