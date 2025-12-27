#include "DialogAddValueToReplace.h"
#include "ui_DialogAddValueToReplace.h"

DialogAddValueToReplace::DialogAddValueToReplace(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddValueToReplace)
{
    ui->setupUi(this);
}

DialogAddValueToReplace::~DialogAddValueToReplace()
{
    delete ui;
}

QString DialogAddValueToReplace::getMarketplaceId() const
{
    return ui->lineEditMarketplaceId->text().trimmed();
}

QString DialogAddValueToReplace::getCountryCode() const
{
    return ui->lineEditCountryCode->text().trimmed();
}

QString DialogAddValueToReplace::getLangCode() const
{
    return ui->lineEditLangCode->text().trimmed();
}

QString DialogAddValueToReplace::getAttributeId() const
{
    return ui->lineEditAttributeId->text().trimmed();
}

QString DialogAddValueToReplace::getValueFrom() const
{
    return ui->lineEditValueFrom->text().trimmed();
}

QString DialogAddValueToReplace::getValueTo() const
{
    return ui->lineEditValueTo->text().trimmed();
}
