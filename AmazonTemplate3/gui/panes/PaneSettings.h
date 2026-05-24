#ifndef PANESETTINGS_H
#define PANESETTINGS_H

#include <QList>
#include <QWidget>

#include "AbstractCli.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneSettings; }
QT_END_NAMESPACE

class PaneSettings : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettings(QWidget *parent = nullptr);
    ~PaneSettings();
    void setAvailableClis(const QList<AbstractCli *> &clis);

private:
    Ui::PaneSettings    *ui;
    QList<AbstractCli *> m_availableClis;

    void _connectSlots();
    void _loadSettings();
};

#endif // PANESETTINGS_H
