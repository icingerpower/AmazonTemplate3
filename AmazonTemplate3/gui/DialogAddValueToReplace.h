#ifndef DIALOGADDVALUETOREPLACE_H
#define DIALOGADDVALUETOREPLACE_H

#include <QDialog>
#include <TemplateFiller.h>

namespace Ui {
class DialogAddValueToReplace;
}

class DialogAddValueToReplace : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddValueToReplace(TemplateFiller *templateFiller, QWidget *parent = nullptr);
    ~DialogAddValueToReplace();

    QString getMarketplace() const;
    QString getCountryCode() const;
    QString getLangCode() const;
    QString getProductType() const;
    QString getAttributeId() const;
    QString getValueFrom() const;
    QString getValueTo() const;

private:
    Ui::DialogAddValueToReplace *ui;
    TemplateFiller *m_templateFiller;
    QList<TemplateFiller::TemplateLocale> m_locales;
    void _updateOkButton();
};

#endif // DIALOGADDVALUETOREPLACE_H
