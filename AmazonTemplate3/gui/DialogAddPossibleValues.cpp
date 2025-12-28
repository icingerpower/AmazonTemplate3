#include "DialogAddPossibleValues.h"
#include "ui_DialogAddPossibleValues.h"

DialogAddPossibleValues::DialogAddPossibleValues(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddPossibleValues)
{
    ui->setupUi(this);
}

DialogAddPossibleValues::~DialogAddPossibleValues()
{
    delete ui;
}

QString DialogAddPossibleValues::getMarketplaceId() const
{
    return ui->lineEditMarketplaceId->text().trimmed();
}

QString DialogAddPossibleValues::getCountryCode() const
{
    return ui->lineEditCountryCode->text().trimmed();
}

QString DialogAddPossibleValues::getLangCode() const
{
    return ui->lineEditLangCode->text().trimmed();
}

QString DialogAddPossibleValues::getProductType() const
{
    return ui->lineEditProductType->text().trimmed();
}

QString DialogAddPossibleValues::getAttributeId() const
{
    return ui->lineEditAttributeId->text().trimmed();
}

QStringList DialogAddPossibleValues::getPossibleValues() const
{
    QStringList result;
    const QString &plainText = ui->textEditPossibleValues->toPlainText();
    const QStringList &lines = plainText.split('\n');
    for (const QString &line : lines)
    {
        const QString &trimmed = line.trimmed();
        if (!trimmed.isEmpty())
        {
            result.append(trimmed);
        }
    }
    return result;
}
