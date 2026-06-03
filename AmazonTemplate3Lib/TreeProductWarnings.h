#pragma once
#include <QAbstractItemModel>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include "TableProductWarnings.h"   // reuse WarningRow struct

class TreeProductWarnings : public QAbstractItemModel
{
    Q_OBJECT
public:
    // Columns — same as before so PaneWarnings enum references still compile
    enum Column {
        ColAsin = 0,
        ColSku,
        ColTitle,
        ColAskAi,         // checkbox (parent rows only) — false = skip AI
        ColAllCountries,  // checkbox: upload to all marketplaces
        ColAllSiblings,   // checkbox: upload to all sibling ASINs with same attributeId
        ColAttribute,     // attributeId for parent row; label for child row
        ColError,         // issueMessage for parent row; empty for child
        ColCount
    };

    struct ChildNode {
        bool    isCurrentValue; // true → shows current value (read-only for saving)
        int     bulletIndex;    // 0–4 for bullet AI children, -1 otherwise
        QString label;          // "Current value", "Bullet 1" … "Bullet 5"
        QString aiValue;        // only meaningful when !isCurrentValue
    };

    struct ViolationNode {
        WarningRow       row;
        QList<ChildNode> children;
        bool             askAi = true;          // false = skip AI, user fills manually
        bool             allCountries = false;  // upload to all marketplaces
        bool             allSiblings  = false;  // upload to all sibling ASINs sharing attributeId
    };

    explicit TreeProductWarnings(QObject *parent = nullptr);
    ~TreeProductWarnings() override;

    void addRow(const WarningRow &row);
    void clear();

    int                  violationCount() const;
    ViolationNode       *violationAt(int i);
    const ViolationNode *violationAt(int i) const;

    // Set one AI value for a given violation's AI children.
    // aiChildIndex: 0 for non-bullet single AI child, 0–4 for bullet children.
    void setAiValue(int violIdx, int aiChildIndex, const QString &value);

    // Loads the per-attribute "Ask AI" exclusion list from the working dir
    // and applies it to any rows already loaded.
    void setWorkingDir(const QString &path);

    // Returns true if the violation at violIdx is checked to be asked AI.
    bool isAskAi(int violIdx) const;

    // Returns true if the violation at violIdx is set to upload to all marketplaces.
    bool isAllCountries(int violIdx) const;
    // Returns true if the violation at violIdx is set to upload to all sibling ASINs
    // that share the same attributeId.
    bool isAllSiblings(int violIdx) const;

    QModelIndex   index(int row, int col,
                        const QModelIndex &parent = {}) const override;
    QModelIndex   parent(const QModelIndex &index) const override;
    int           rowCount(const QModelIndex &parent = {}) const override;
    int           columnCount(const QModelIndex &parent = {}) const override;
    QVariant      data(const QModelIndex &index,
                       int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation o,
                             int role = Qt::DisplayRole) const override;
    bool          setData(const QModelIndex &index, const QVariant &value,
                          int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    void _loadExclusions();
    void _saveExclusions() const;
    void _loadUploadOptions();
    void _saveUploadOptions() const;

    QList<ViolationNode *> m_violations;
    QString                m_workingDir;
    QSet<QString>          m_excludedAttrIds;     // lower-cased; persisted as JSON
    QSet<QString>          m_allCountriesAttrIds; // lower-cased
    QSet<QString>          m_allSiblingsAttrIds;  // lower-cased
    static const QStringList HEADER;
};
