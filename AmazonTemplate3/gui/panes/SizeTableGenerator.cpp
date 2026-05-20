#include "SizeTableGenerator.h"
#include "fillers/FillerSize.h"
#include <QStandardItemModel>
#include <QObject>
#include <algorithm>

QList<SizeTableGenerator::Category> SizeTableGenerator::allCategories()
{
    return {Category::ClothingWomen, Category::ClothingMen,
            Category::ShoesWomen,   Category::ShoesMen};
}

QString SizeTableGenerator::displayName(Category cat)
{
    switch (cat) {
    case Category::ClothingWomen: return QObject::tr("Clothing – Women");
    case Category::ClothingMen:   return QObject::tr("Clothing – Men");
    case Category::ShoesWomen:    return QObject::tr("Shoes – Women");
    case Category::ShoesMen:      return QObject::tr("Shoes – Men");
    }
    return {};
}

QStringList SizeTableGenerator::referenceKeys(Category cat)
{
    QStringList keys;
    switch (cat) {
    case Category::ClothingWomen:
        for (const auto &row : FillerSize::CLOTHE_FEMALE_ADULT_SIZES)
            keys << QString::number(row.value("FR"));
        break;
    case Category::ClothingMen:
        for (const auto &row : FillerSize::CLOTHE_MALE_ADULT_SIZES)
            keys << QString::number(row.value("FR"));
        break;
    case Category::ShoesWomen:
        for (const auto &row : FillerSize::SHOE_FEMALE_ADULT_SIZES)
            keys << QString::number(static_cast<int>(row.value("FR")));
        break;
    case Category::ShoesMen:
        for (const auto &row : FillerSize::SHOE_MALE_ADULT_SIZES)
            keys << QString::number(static_cast<int>(row.value("FR")));
        break;
    }
    keys.removeDuplicates();
    return keys;
}

QPair<QString,QString> SizeTableGenerator::guessRange(Category cat, const QStringList &rawSizes)
{
    const QStringList refs = referenceKeys(cat);
    QStringList matched;
    for (const QString &raw : rawSizes) {
        bool ok = false;
        const int val = raw.trimmed().toInt(&ok);
        if (!ok) {
            const QString stripped = raw.trimmed().split(' ').first();
            const int v2 = stripped.toInt(&ok);
            if (ok) {
                const QString key = QString::number(v2);
                if (refs.contains(key) && !matched.contains(key))
                    matched << key;
            }
            continue;
        }
        const QString key = QString::number(val);
        if (refs.contains(key) && !matched.contains(key))
            matched << key;
    }
    if (matched.isEmpty())
        return {};
    QStringList sorted;
    for (const QString &ref : refs)
        if (matched.contains(ref))
            sorted << ref;
    return {sorted.first(), sorted.last()};
}

static QString formatShoeValue(double val, bool isCm)
{
    if (isCm) {
        return QString::number(val, 'f', 1) + QLatin1String(" cm");
    }
    if (val == static_cast<int>(val))
        return QString::number(static_cast<int>(val));
    return QString::number(val, 'f', 1);
}

QStandardItemModel* SizeTableGenerator::build(Category cat,
                                               const QString &euFrom,
                                               const QString &euTo,
                                               QObject *parent)
{
    auto *model = new QStandardItemModel(parent);

    if (cat == Category::ClothingWomen || cat == Category::ClothingMen) {
        struct Col { QString code; QString header; };
        const QList<Col> cols{
            {"FR","FR"},{"BE","BE"},{"ES","ES"},{"DE","DE"},{"NL","NL"},
            {"SE","SE"},{"PL","PL"},{"IT","IT"},{"TR","TR"},
            {"UK","UK"},{"IE","IE"},{"AU","AU"},
            {"COM","US"},{"CA","CA"},
            {"JP","JP"},{"AE","AE"},{"MX","MX"},{"SA","SA"},{"SG","SG"}
        };
        model->setColumnCount(cols.size());
        for (int c = 0; c < cols.size(); ++c)
            model->setHeaderData(c, Qt::Horizontal, cols[c].header);

        const auto &sizeList = (cat == Category::ClothingWomen)
            ? FillerSize::CLOTHE_FEMALE_ADULT_SIZES
            : FillerSize::CLOTHE_MALE_ADULT_SIZES;

        int fromIdx = 0, toIdx = sizeList.size() - 1;
        for (int r = 0; r < sizeList.size(); ++r) {
            if (QString::number(sizeList[r].value("FR")) == euFrom) fromIdx = r;
            if (QString::number(sizeList[r].value("FR")) == euTo)   toIdx   = r;
        }
        if (fromIdx > toIdx) std::swap(fromIdx, toIdx);

        for (int r = fromIdx; r <= toIdx; ++r) {
            const auto &row = sizeList[r];
            QList<QStandardItem*> items;
            for (const auto &col : cols)
                items << new QStandardItem(QString::number(row.value(col.code)));
            model->appendRow(items);
        }

    } else {
        struct Group { QString label; QString rep; bool isCm; };
        const QList<Group> groups{
            {"EU", "FR",  false},
            {"US", "COM", false},
            {"UK", "UK",  false},
            {"JP", "JP",  true},
        };
        model->setColumnCount(groups.size());
        for (int c = 0; c < groups.size(); ++c)
            model->setHeaderData(c, Qt::Horizontal, groups[c].label);

        const auto &sizeList = (cat == Category::ShoesWomen)
            ? FillerSize::SHOE_FEMALE_ADULT_SIZES
            : FillerSize::SHOE_MALE_ADULT_SIZES;

        int fromIdx = 0, toIdx = sizeList.size() - 1;
        for (int r = 0; r < sizeList.size(); ++r) {
            const int eu = static_cast<int>(sizeList[r].value("FR"));
            if (QString::number(eu) == euFrom) fromIdx = r;
            if (QString::number(eu) == euTo)   toIdx   = r;
        }
        if (fromIdx > toIdx) std::swap(fromIdx, toIdx);

        for (int r = fromIdx; r <= toIdx; ++r) {
            const auto &row = sizeList[r];
            QList<QStandardItem*> items;
            for (const auto &g : groups)
                items << new QStandardItem(formatShoeValue(row.value(g.rep), g.isCm));
            model->appendRow(items);
        }
    }

    return model;
}
