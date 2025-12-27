#ifndef DIALOGVALIDATEMANDATORY_H
#define DIALOGVALIDATEMANDATORY_H

#include <QDialog>

#include "TemplateFiller.h"

namespace Ui {
class DialogValidateMandatory;
}

class DialogValidateMandatory : public QDialog
{
    Q_OBJECT

public:
    explicit DialogValidateMandatory(
            const TemplateFiller::AttributesToValidate &attributesToValidate,
            QWidget *parent = nullptr);
    QSet<QString> getAttributeValidatedMandatory() const;
    QSet<QString> getAttributeValidatedNotMandatory() const;
    ~DialogValidateMandatory();

private:
    Ui::DialogValidateMandatory *ui;
};

#endif // DIALOGVALIDATEMANDATORY_H
