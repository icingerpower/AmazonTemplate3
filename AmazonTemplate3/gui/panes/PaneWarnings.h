#ifndef PANEWARNINGS_H
#define PANEWARNINGS_H

#include <QDir>
#include <QList>
#include <QWidget>

#include "AbstractCli.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneWarnings; }
QT_END_NAMESPACE

class PaneWarnings : public QWidget
{
    Q_OBJECT

public:
    explicit PaneWarnings(QWidget *parent = nullptr);
    ~PaneWarnings();
    void setWorkingDir(const QDir &workingDir);
    void setAvailableClis(const QList<AbstractCli *> &clis);

private:
    Ui::PaneWarnings   *ui;
    QDir                m_workingDir;
    QList<AbstractCli *> m_availableClis;

    void _populateMarketplaces();
    void _loadSettings();
};

#endif // PANEWARNINGS_H
