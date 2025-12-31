#ifndef DIALOGADDVALUETOREPLACE_H
#define DIALOGADDVALUETOREPLACE_H

#include <QDialog>

namespace Ui {
class DialogAddValueToReplace;
}

class DialogAddValueToReplace : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddValueToReplace(QWidget *parent = nullptr);
    ~DialogAddValueToReplace();

    QString getMarketplaceId() const;
    QString getCountryCode() const;
    QString getLangCode() const;
    QString getProductType() const;
    QString getAttributeId() const;
    QString getValueFrom() const;
    QString getValueTo() const;

private:
    Ui::DialogAddValueToReplace *ui;
};

#endif // DIALOGADDVALUETOREPLACE_H
