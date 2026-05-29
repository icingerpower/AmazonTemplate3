#pragma once
#include <QDialog>
#include <QImage>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include "APlusContent.h"

class QCheckBox;
class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;

class APlusUploadDialog : public QDialog {
    Q_OBJECT
public:
    struct ElementInfo {
        QString          id;
        QString          displayName;
        APlusElementType type;
        QString          imagePath;
        QString          textContent;
        QImage           thumbnail;
    };

    // colorNames: display names of color variants (e.g. {"Yellow", "Orange"})
    // from PaneSizing::m_colorVariants first elements.
    // If empty, the table has a single "Images" column.
    explicit APlusUploadDialog(const QList<ElementInfo> &elements,
                                const QList<QPair<QString,QString>> &marketplaces,
                                const QStringList &colorNames,
                                bool submitForApprovalDefault = true,
                                QWidget *parent = nullptr);

    QStringList                    selectedMarketplaceIds() const;
    bool                           includeSizeChart()       const;
    bool                           includeFaq()             const;
    // One list per color column that has >= 1 checked image, in column order.
    QList<QList<ElementInfo>>      selectedImagesByColor()  const;
    bool                           shouldSubmitForApproval()const;

    static QString sizeChartKeyForMarketplace(const QString &marketplaceId);
    static QString faqLangKeyForMarketplace   (const QString &marketplaceId);

private slots:
    void onOptionChanged();
    void onTableItemChanged(QTableWidgetItem *item);

private:
    void _buildTable();
    void _applyDefaultChecks();
    void _updateBudget();
    void _updateUploadButton();
    int  _budget() const;
    bool _isCellEnabled(int row, int col) const;  // based on color-specificity

    QList<ElementInfo>            m_sizeCharts;
    QList<ElementInfo>            m_faqs;
    QList<ElementInfo>            m_images;
    QList<QPair<QString,QString>> m_marketplaces;
    QStringList                   m_colorNames;   // display names: "Yellow", "Orange"
    QStringList                   m_colorKeys;    // safe keys: "yellow", "orange"

    QList<QCheckBox*>  m_countryChecks;
    QCheckBox         *m_inclSizeChart = nullptr;
    QCheckBox         *m_inclFaq       = nullptr;
    QTableWidget      *m_table         = nullptr;
    QLabel            *m_budgetLabel   = nullptr;
    QCheckBox         *m_submitCheck   = nullptr;
    QPushButton       *m_uploadButton  = nullptr;
};
