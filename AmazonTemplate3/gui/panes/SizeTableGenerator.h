#ifndef SIZETABLEGENERATOR_H
#define SIZETABLEGENERATOR_H

#include <QStringList>
#include <QPair>

class QStandardItemModel;
class QObject;

class SizeTableGenerator
{
public:
    enum class Category { ClothingWomen, ClothingMen, ShoesWomen, ShoesMen };

    static QList<Category> allCategories();
    static QString displayName(Category cat);

    // EU reference sizes used to populate comboBoxSizeFrom / comboBoxSizeTo
    static QStringList referenceKeys(Category cat);

    // Try to guess min/max EU key from raw size strings extracted from the tree model
    static QPair<QString, QString> guessRange(Category cat, const QStringList& rawSizes);

    // Build a QStandardItemModel for tableViewSizing
    // Rows = each EU reference size in [euFrom, euTo]
    // Columns = all relevant country codes / groups (see implementation)
    static QStandardItemModel* build(Category cat,
                                     const QString& euFrom,
                                     const QString& euTo,
                                     QObject* parent = nullptr);
};

#endif // SIZETABLEGENERATOR_H
