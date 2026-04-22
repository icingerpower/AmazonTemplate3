#ifndef PANESETTINGS_H
#define PANESETTINGS_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class PaneSettings; }
QT_END_NAMESPACE

class PaneSettings : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettings(QWidget *parent = nullptr);
    ~PaneSettings();

private:
    Ui::PaneSettings *ui;
    void _connectSlots();
    void _loadSettings();
};

#endif // PANESETTINGS_H
