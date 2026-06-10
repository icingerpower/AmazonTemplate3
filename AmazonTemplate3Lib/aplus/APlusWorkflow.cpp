#include "APlusWorkflow.h"
#include "APlusContent.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>

namespace {

QString colorSafeId(const QString &color)
{
    QString result;
    for (const QChar &c : color.toLower()) {
        if (c.isLetterOrNumber())
            result += c;
        else if (!result.isEmpty() && result.back() != QLatin1Char('-'))
            result += QLatin1Char('-');
    }
    while (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    return result.isEmpty() ? QStringLiteral("unknown") : result;
}

// Builds the common preamble: product description + main image hint + instructions block.
QString buildPreamble(const QString &productDesc,
                      const QString &mainImageHint,
                      const QString &instructions)
{
    QString p;
    if (!productDesc.isEmpty())
        p += QCoreApplication::translate("APlusWorkflow", "Product:\n")
             + productDesc + QStringLiteral("\n\n");
    if (!mainImageHint.isEmpty())
        p += mainImageHint + QStringLiteral("\n\n");
    if (!instructions.isEmpty())
        p += QCoreApplication::translate("APlusWorkflow", "Instructions:\n")
             + instructions + QStringLiteral("\n\n");
    return p;
}

QStringList orderColors(const QStringList &colors, const QString &focusColor)
{
    QStringList ordered;
    if (!focusColor.isEmpty() && colors.contains(focusColor))
        ordered << focusColor;
    QStringList rest;
    for (const QString &c : colors) {
        if (c != focusColor && !c.isEmpty())
            rest << c;
    }
    std::sort(rest.begin(), rest.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    ordered += rest;
    return ordered;
}

QString workflowSettingKey(const QString &workflowId, const QString &key)
{
    return QStringLiteral("aplus/workflows/%1/%2").arg(workflowId, key);
}

} // namespace

// ============================================================================
// GenericAPlusWorkflow
// ============================================================================
class GenericAPlusWorkflow : public APlusWorkflow
{
public:
    QString id()   const override { return QStringLiteral("generic"); }
    QString name() const override
    { return QCoreApplication::translate("APlusWorkflow", "Generic"); }

    int stepCount() const override { return 2; }
    QString stepName(int step) const override
    {
        switch (step) {
        case 0: return QCoreApplication::translate("APlusWorkflow", "Desktop image");
        case 1: return QCoreApplication::translate("APlusWorkflow", "Mobile image");
        default: return {};
        }
    }

    QString defaultDesktopPrompt(int step) const override
    {
        QSettings s;
        QString val = s.value(workflowSettingKey(id(), QStringLiteral("step%1_desktop").arg(step))).toString();
        if (val.isEmpty()) {
            if (step == 0) return QCoreApplication::translate("APlusWorkflow",
                "Generate a professional Amazon A+ desktop marketing image "
                "(970x600 px, landscape). Save as desktop.png in the current directory.");
        }
        return val;
    }

    QString defaultMobilePrompt(int step) const override
    {
        QSettings s;
        QString val = s.value(workflowSettingKey(id(), QStringLiteral("step%1_mobile").arg(step))).toString();
        if (val.isEmpty()) {
            if (step == 1) return QCoreApplication::translate("APlusWorkflow",
                "Generate a professional Amazon A+ mobile marketing image "
                "(600x600 px, square). Save as mobile.png in the current directory.");
        }
        return val;
    }

    int versionCount(int step) const override
    {
        QSettings s;
        return s.value(workflowSettingKey(id(), QStringLiteral("step%1_versionCount").arg(step)), 1).toInt();
    }

    void setDefaultDesktopPrompt(int step, const QString &prompt) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), QStringLiteral("step%1_desktop").arg(step)), prompt);
    }

    void setDefaultMobilePrompt(int step, const QString &prompt) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), QStringLiteral("step%1_mobile").arg(step)), prompt);
    }

    void setVersionCount(int step, int count) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), QStringLiteral("step%1_versionCount").arg(step)), count);
    }

    QList<ImageSlotSpec> buildSlots(
        const APlusContent *content,
        const QStringList  & /*colors*/,
        const QString      & /*focusColor*/,
        const QString      &productDesc,
        const QString      &mainImageHint,
        const QStringList  &stepInstructions) const override
    {
        const QString desktopInstr = stepInstructions.value(0);
        const QString mobileInstr  = stepInstructions.value(1);

        // Collect existing image elements; if none, create two defaults.
        QList<QPair<QString, QString>> elementIds; // (id, displayName)
        if (content) {
            for (const APlusElement &el : content->elements()) {
                if (el.type == APlusElementType::Image)
                    elementIds.append({el.id, el.displayName});
            }
        }
        if (elementIds.isEmpty()) {
            elementIds.append({QStringLiteral("image_1"),
                QCoreApplication::translate("APlusWorkflow", "Image 1")});
            elementIds.append({QStringLiteral("image_2"),
                QCoreApplication::translate("APlusWorkflow", "Image 2")});
        }

        const QString desktopSpec = defaultDesktopPrompt(0);
        const QString mobileSpec  = defaultMobilePrompt(1);

        const QString desktopPreamble = buildPreamble(productDesc, mainImageHint, desktopInstr);
        const QString mobilePreamble  = buildPreamble(productDesc, mainImageHint, mobileInstr);

        QList<ImageSlotSpec> result;
        int i = 0;
        for (const auto &[eid, dn] : std::as_const(elementIds)) {
            ImageSlotSpec spec;
            spec.elementId     = eid;
            spec.displayName   = dn;
            spec.desktopPrompt = desktopPreamble + desktopSpec;
            spec.mobilePrompt  = mobilePreamble  + mobileSpec;
            spec.versionCount  = versionCount(i % stepCount());
            result << spec;
            i++;
        }
        return result;
    }
};

// ============================================================================
// ClothingAPlusWorkflow
// ============================================================================
class ClothingAPlusWorkflow : public APlusWorkflow
{
public:
    QString id()   const override { return QStringLiteral("clothing"); }
    QString name() const override
    { return QCoreApplication::translate("APlusWorkflow", "Clothing / Shoes"); }

    int stepCount() const override { return 3; }
    QString stepName(int step) const override
    {
        switch (step) {
        case 0: return QCoreApplication::translate("APlusWorkflow", "Group Shot");
        case 1: return QCoreApplication::translate("APlusWorkflow", "Per-Color");
        case 2: return QCoreApplication::translate("APlusWorkflow", "Detail / Fabric");
        default: return {};
        }
    }

    QString defaultDesktopPrompt(int step) const override
    {
        QSettings s;
        QString val = s.value(workflowSettingKey(id(), QStringLiteral("step%1_desktop").arg(step))).toString();
        if (val.isEmpty()) {
            switch (step) {
            case 0: return QCoreApplication::translate("APlusWorkflow",
                "Generate a professional Amazon A+ desktop lifestyle marketing image "
                "(970x600 px, landscape). Show %1 models in the same scene, each "
                "wearing the product in a different color: %2. "
                "Choose a background that matches the product's typical occasion of use and "
                "raises perceived value, status and desire to buy. "
                "Aspirational, premium quality, no text overlays, no watermarks. "
                "Save as desktop.png in the current directory.");
            case 1: return QCoreApplication::translate("APlusWorkflow",
                "Generate a professional Amazon A+ desktop marketing image (970x600 px, landscape) "
                "showing 3 models all wearing the product in %1. "
                "Use 3 different angles or poses to highlight the cut and fit. "
                "Aspirational lifestyle scene, premium quality, raises perceived value and "
                "desire to buy. No text overlays, no watermarks. "
                "Save as desktop.png in the current directory.");
            case 2: return QCoreApplication::translate("APlusWorkflow",
                "Generate a professional Amazon A+ desktop image (970x600 px, landscape) "
                "showing one model wearing the product%1, paired with a prominent close-up "
                "of the fabric texture or a key design feature (stitching, weave, hardware, "
                "trim — whichever best highlights craftsmanship). "
                "%2 "
                "Goal: convey quality, craftsmanship and premium feel. "
                "No text overlays, no watermarks. "
                "Save as desktop.png in the current directory.");
            }
        }
        return val;
    }

    QString defaultMobilePrompt(int step) const override
    {
        QSettings s;
        QString val = s.value(workflowSettingKey(id(), QStringLiteral("step%1_mobile").arg(step))).toString();
        if (val.isEmpty()) {
            switch (step) {
            case 0: return QCoreApplication::translate("APlusWorkflow",
                "Generate a professional Amazon A+ mobile lifestyle marketing image "
                "(600x600 px, square). Show %1 models in the same scene, each wearing the "
                "product in a different color: %2. "
                "Background matches the product's typical occasion of use, aspirational, "
                "premium quality, no text overlays. "
                "Save as mobile.png in the current directory.");
            case 1: return QCoreApplication::translate("APlusWorkflow",
                "Generate a professional Amazon A+ mobile marketing image (600x600 px, square) "
                "showing 2 models wearing the product in %1. "
                "Aspirational lifestyle scene, premium quality, no text overlays. "
                "Save as mobile.png in the current directory.");
            case 2: return QCoreApplication::translate("APlusWorkflow",
                "Generate a professional Amazon A+ mobile image (600x600 px, square) "
                "showing one model wearing the product%1 with a prominent close-up of "
                "the fabric texture or key design feature. "
                "%2 "
                "Convey quality and craftsmanship, no text overlays. "
                "Save as mobile.png in the current directory.");
            }
        }
        return val;
    }

    int versionCount(int step) const override
    {
        QSettings s;
        return s.value(workflowSettingKey(id(), QStringLiteral("step%1_versionCount").arg(step)), 1).toInt();
    }

    void setDefaultDesktopPrompt(int step, const QString &prompt) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), QStringLiteral("step%1_desktop").arg(step)), prompt);
    }

    void setDefaultMobilePrompt(int step, const QString &prompt) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), QStringLiteral("step%1_mobile").arg(step)), prompt);
    }

    void setVersionCount(int step, int count) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), QStringLiteral("step%1_versionCount").arg(step)), count);
    }

    QList<ImageSlotSpec> buildSlots(
        const APlusContent * /*content*/,
        const QStringList  &colors,
        const QString      &focusColor,
        const QString      &productDesc,
        const QString      &mainImageHint,
        const QStringList  &stepInstructions) const override
    {
        QList<ImageSlotSpec> result;

        const QStringList orderedColors = orderColors(colors, focusColor);

        const QString groupInstr  = stepInstructions.value(0);
        const QString colorInstr  = stepInstructions.value(1);
        const QString detailInstr = stepInstructions.value(2);

        // 1. Group shot — only if 2+ distinct colors
        if (orderedColors.size() >= 2) {
            const int desktopModels = std::min(static_cast<int>(orderedColors.size()), 4);
            const int mobileModels  = std::min(static_cast<int>(orderedColors.size()), 3);
            const QStringList desktopColors = orderedColors.mid(0, desktopModels);
            const QStringList mobileColors  = orderedColors.mid(0, mobileModels);

            ImageSlotSpec spec;
            spec.elementId   = QStringLiteral("image_group");
            spec.displayName = QCoreApplication::translate("APlusWorkflow", "Group Shot");

            const QString desktopSpec = defaultDesktopPrompt(0)
                .arg(desktopModels)
                .arg(desktopColors.join(QStringLiteral(", ")));

            const QString mobileSpec = defaultMobilePrompt(0)
                .arg(mobileModels)
                .arg(mobileColors.join(QStringLiteral(", ")));

            spec.desktopPrompt = buildPreamble(productDesc, mainImageHint, groupInstr) + desktopSpec;
            spec.mobilePrompt  = buildPreamble(productDesc, mainImageHint, groupInstr) + mobileSpec;
            spec.versionCount  = versionCount(0);
            result << spec;
        }

        // 2. Per-color images — focus color first, then alphabetical
        for (const QString &color : orderedColors) {
            ImageSlotSpec spec;
            spec.elementId   = QStringLiteral("image_color_") + colorSafeId(color);
            spec.displayName = QCoreApplication::translate("APlusWorkflow", "Color — %1").arg(color);

            const QString desktopSpec = defaultDesktopPrompt(1).arg(color);
            const QString mobileSpec = defaultMobilePrompt(1).arg(color);

            spec.desktopPrompt = buildPreamble(productDesc, mainImageHint, colorInstr) + desktopSpec;
            spec.mobilePrompt  = buildPreamble(productDesc, mainImageHint, colorInstr) + mobileSpec;
            spec.versionCount  = versionCount(1);
            result << spec;
        }

        // 3. Detail / Fabric shot
        {
            ImageSlotSpec spec;
            spec.elementId   = QStringLiteral("image_detail");
            spec.displayName = QCoreApplication::translate("APlusWorkflow", "Detail / Fabric");

            const QString colorMention = focusColor.isEmpty()
                ? QString{}
                : QCoreApplication::translate("APlusWorkflow", " in %1").arg(focusColor);

            // If a reference image is available the close-up must be faithful to it;
            // if not, the detail can be impressionistic / suggested.
            const QString fabricAccuracy = mainImageHint.isEmpty()
                ? QCoreApplication::translate("APlusWorkflow",
                    "No reference photo is available, so the close-up may suggest fabric "
                    "texture and craftsmanship freely — keep it plausible but do not "
                    "over-invent intricate patterns.")
                : QCoreApplication::translate("APlusWorkflow",
                    "The close-up must faithfully reproduce the actual fabric texture and "
                    "design details visible in the reference image — do not invent, add or "
                    "alter the fabric pattern, weave or stitching.");

            const QString desktopSpec = defaultDesktopPrompt(2).arg(colorMention, fabricAccuracy);
            const QString mobileSpec = defaultMobilePrompt(2).arg(colorMention, fabricAccuracy);

            spec.desktopPrompt = buildPreamble(productDesc, mainImageHint, detailInstr) + desktopSpec;
            spec.mobilePrompt  = buildPreamble(productDesc, mainImageHint, detailInstr) + mobileSpec;
            spec.versionCount  = versionCount(2);
            result << spec;
        }

        return result;
    }
};

// ============================================================================
// Factory
// ============================================================================
static GenericAPlusWorkflow  sGeneric;
static ClothingAPlusWorkflow sClothing;

const QList<APlusWorkflow *> &APlusWorkflow::all()
{
    static QList<APlusWorkflow *> lst = { &sClothing, &sGeneric };
    return lst;
}

APlusWorkflow *APlusWorkflow::findById(const QString &id)
{
    for (APlusWorkflow *wf : APlusWorkflow::all())
        if (wf->id() == id)
            return wf;
    return nullptr;
}
