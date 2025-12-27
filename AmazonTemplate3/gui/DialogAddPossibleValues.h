#ifndef DIALOGADDPOSSIBLEVALUES_H
#define DIALOGADDPOSSIBLEVALUES_H

#include <QDialog>

namespace Ui {
class DialogAddPossibleValues;
}

class DialogAddPossibleValues : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddPossibleValues(QWidget *parent = nullptr);
    ~DialogAddPossibleValues();

    QString getMarketplaceId() const;
    QString getCountryCode() const;
    QString getLangCode() const;
    QString getAttributeId() const;
    QStringList getPossibleValues() const;

private:
    Ui::DialogAddPossibleValues *ui;
};

#endif // DIALOGADDPOSSIBLEVALUES_H
