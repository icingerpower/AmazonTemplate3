#ifndef PANESIZING_H
#define PANESIZING_H

#include <QWidget>
#include <QDir>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class PaneSizing; }
QT_END_NAMESPACE

class AmazonCatalogApi;
class TreeSizingAsins;
class QStandardItemModel;

class PaneSizing : public QWidget
{
    Q_OBJECT
public:
    explicit PaneSizing(QWidget *parent = nullptr);
    ~PaneSizing();

    void setWorkingDir(const QDir &workingDir);

private slots:
    void onAddFromAsinClicked();
    void onAddFromTemplateClicked();
    void onSizeTypeChanged(int index);
    void onGenSizeTablesClicked();
    void updateButtonStates();

private:
    Ui::PaneSizing *ui;
    std::unique_ptr<AmazonCatalogApi> m_api;
    std::unique_ptr<TreeSizingAsins>  m_treeModel;
    QStandardItemModel*               m_sizeTableModel = nullptr;
    QString m_currentMarketplaceId{QStringLiteral("A13V1IB3VIYZZH")};
    bool    m_generatedSuccessfully = false;

    void _ensureModel(const QDir &dir);
    void _refreshApi();
    void _populateSizeRangeCombos();
    void _tryGuessSizeRange();
};

#endif // PANESIZING_H
