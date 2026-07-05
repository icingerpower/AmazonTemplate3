#ifndef APLUSTREEMODEL_H
#define APLUSTREEMODEL_H

#include <QAbstractItemModel>
#include <QList>
#include <QModelIndex>
#include <QSet>
#include <QString>
#include <QVariant>

#include "APlusContent.h"

class APlusTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column { Name = 0, Desktop, Mobile, COLUMN_COUNT };

    explicit APlusTreeModel(APlusContent *content, QObject *parent = nullptr);

    struct Location {
        int family  = -1;
        int version = -1;
        int lang    = -1;
        bool isValid()    const { return family >= 0; }
        bool isVersion()  const { return family >= 0 && version >= 0 && lang < 0; }
        bool isLanguage() const { return family >= 0 && version >= 0 && lang >= 0; }
    };
    Location locate(const QModelIndex &idx) const;

    QString absoluteFilePath(const Location &loc, bool desktop) const;

    // Returns the family row index for an element id (matches by family rules),
    // or -1 if no family contains that element.
    int familyIndexForElement(const QString &elementId) const;

    // Returns the stable familyId string for the given family row, or {} if out of range.
    QString familyIdAt(int familyRow) const;

    // Returns the underlying APlusContent element index referenced by a location:
    // - language node → the variant element at loc.lang
    // - version node  → the family's base element
    // - else          → -1
    int elementIndexForLocation(const Location &loc) const;

    // Families whose familyId is in this set are omitted from the tree
    // (used to hide elements of excluded colors). Takes effect on rebuild().
    void setHiddenFamilyIds(const QSet<QString> &ids) { m_hiddenFamilyIds = ids; }

    void rebuild();

    QModelIndex index(int row, int col, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &idx) const override;
    int rowCount   (const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data      (const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &idx) const override;

private:
    static constexpr quintptr kFamilyId       = 0;
    static constexpr quintptr kVersionIdMask  = 0xFFFF;

    // A family groups one (single-variant — images) or more (multi-variant —
    // size_chart / faq) APlusContent elements together. The base element is
    // the one used for version listing (level 2) and for single-variant data.
    struct FamilyNode {
        QString     familyId;          // "size_chart", "faq", or element.id for singletons
        QString     displayName;       // "Size Chart", "FAQ", or element.displayName
        int         baseElemIdx = -1;  // index into APlusContent::elements()
        QList<int>  variantElemIndices; // indices, in display order
        QStringList langLabels;         // labels parallel to variantElemIndices
    };

    void _rebuildFamilies();

    QModelIndex _makeFamilyIndex(int familyRow, int col) const;
    QModelIndex _makeVersionIndex(int familyRow, int versionRow, int col) const;
    QModelIndex _makeLanguageIndex(int familyRow, int versionRow, int langRow, int col) const;

    static QString _langLabelForSuffix(const QString &suffix);

    APlusContent       *m_content = nullptr;
    QList<FamilyNode>   m_families;
    QSet<QString>       m_hiddenFamilyIds;
};

#endif // APLUSTREEMODEL_H
