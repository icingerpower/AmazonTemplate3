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

// Returns the QSettings key for a workflow prompt.
// When categoryKey is set the key is scoped per category so each size category
// stores its own prompt set. Falls back to the uncategorised key path when empty
// (migration: old prompts saved before the category scoping was introduced).
QString workflowSettingKey(const QString &workflowId,
                           const QString &categoryKey,
                           const QString &key)
{
    if (categoryKey.isEmpty())
        return QStringLiteral("aplus/workflows/%1/%2").arg(workflowId, key);
    const QString safeCat = QString(categoryKey)
        .replace(QLatin1Char(' '),  QLatin1Char('_'))
        .replace(QLatin1Char('/'),  QLatin1Char('_'))
        .replace(QString::fromUtf8("–"), QStringLiteral("-"));
    return QStringLiteral("aplus/workflows/%1/%2/%3").arg(workflowId, safeCat, key);
}

// Read helper: tries the category-scoped key first, falls back to the legacy
// uncategorised key so prompts saved before category scoping still load.
QString readWorkflowSetting(const QSettings &s,
                            const QString &workflowId,
                            const QString &categoryKey,
                            const QString &key)
{
    const QString catKey = workflowSettingKey(workflowId, categoryKey, key);
    const QString val    = s.value(catKey).toString();
    if (!val.isEmpty() || categoryKey.isEmpty())
        return val;
    return s.value(workflowSettingKey(workflowId, {}, key)).toString();
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
        QString val = readWorkflowSetting(s, id(), categoryKey(), QStringLiteral("step%1_desktop").arg(step));
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
        QString val = readWorkflowSetting(s, id(), categoryKey(), QStringLiteral("step%1_mobile").arg(step));
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
        const QString key = QStringLiteral("step%1_versionCount").arg(step);
        const QString catKey = workflowSettingKey(id(), categoryKey(), key);
        if (s.contains(catKey)) return s.value(catKey, 1).toInt();
        return s.value(workflowSettingKey(id(), {}, key), 1).toInt();
    }

    void setDefaultDesktopPrompt(int step, const QString &prompt) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), categoryKey(), QStringLiteral("step%1_desktop").arg(step)), prompt);
    }

    void setDefaultMobilePrompt(int step, const QString &prompt) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), categoryKey(), QStringLiteral("step%1_mobile").arg(step)), prompt);
    }

    void setVersionCount(int step, int count) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), categoryKey(), QStringLiteral("step%1_versionCount").arg(step)), count);
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

    int stepCount() const override { return 4; }
    QString stepName(int step) const override
    {
        switch (step) {
        case 0: return QCoreApplication::translate("APlusWorkflow", "Group Shot");
        case 1: return QCoreApplication::translate("APlusWorkflow", "Per-Color");
        case 2: return QCoreApplication::translate("APlusWorkflow", "Detail / Fabric");
        case 3: return QCoreApplication::translate("APlusWorkflow", "Aspirational Scene");
        default: return {};
        }
    }

    bool isShoeCategory() const
    {
        return categoryKey().contains(QLatin1String("Shoes"), Qt::CaseInsensitive)
            || categoryKey().contains(QLatin1String("Chaussures"), Qt::CaseInsensitive);
    }

    QString defaultDesktopPrompt(int step) const override
    {
        QSettings s;
        QString val = readWorkflowSetting(s, id(), categoryKey(), QStringLiteral("step%1_desktop").arg(step));
        if (val.isEmpty()) {
            if (isShoeCategory()) {
                switch (step) {
                case 0: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ desktop image (970x600 px, landscape) "
                    "showcasing all color variants of the shoe together: %2. "
                    "Arrange the pairs in an aspirational flat-lay or on a clean minimal surface — "
                    "the shoes are the sole focus, no models, no competing props. "
                    "Strong directional lighting to reveal silhouette, heel height and material finish. "
                    "No text overlays, no watermarks. "
                    "Save as desktop.png in the current directory.");
                case 1: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ desktop image (970x600 px, landscape) "
                    "of the shoe in %1. Show three complementary angles: "
                    "a 3/4 front view, a side profile highlighting the heel height and silhouette, "
                    "and a rear or detail view showing heel construction or toe shape. "
                    "Shoe is the clear hero — clean or minimally styled background, "
                    "aspirational, premium quality. No text overlays, no watermarks. "
                    "Save as desktop.png in the current directory.");
                case 2: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ desktop image (970x600 px, landscape) "
                    "featuring a close-up detail study of the shoe%1. "
                    "Focus on the most distinctive design elements: heel architecture, "
                    "sole construction, upper material texture, hardware (buckle, strap, "
                    "embellishment) or toe shape — whichever best conveys craftsmanship. "
                    "%2 "
                    "High-contrast lighting to reveal texture and finish. "
                    "No text overlays, no watermarks. "
                    "Save as desktop.png in the current directory.");
                case 3: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ desktop image (970x600 px, landscape) "
                    "that sells the aspiration of wearing this shoe rather than documenting it. "
                    "The shoe in %1 is the sole subject — no models, no faces, no competing props. "
                    "Select one aspirational setting that matches the shoe's color and mood:\n"
                    "  rose gold or blush → marble foyer of a gala or art opening at dusk;\n"
                    "  black → rooftop terrace, theater lobby or rain-slicked cobblestones at night;\n"
                    "  white or ivory → sun-drenched coastal terrace or garden event;\n"
                    "  metallic silver or gold → hotel corridor or ballroom threshold;\n"
                    "  nude or tan → boutique salon, art gallery or champagne brunch setting.\n"
                    "The shoe rests on a surface consistent with that setting "
                    "(marble, polished hardwood, cobblestone or natural stone). "
                    "Cinematic framing: low or mid-angle, soft ambient lighting, shallow depth of field — "
                    "the background suggests the occasion without competing with the shoe. "
                    "The image should feel like the opening frame of a luxury fragrance advertisement. "
                    "No text overlays, no watermarks. Save as desktop.png in the current directory.");
                }
            } else {
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
                case 3: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ desktop lifestyle image (970x600 px, landscape) "
                    "that sells the aspiration of wearing this %1 garment rather than simply documenting it. "
                    "Show one model in an environment that matches the garment's occasion and elevates desire: "
                    "choose a setting that reflects the product's mood — outdoor market, rooftop, "
                    "hotel lobby, art gallery, garden event or similar — without clichés. "
                    "The garment is the undisputed hero: well-lit, clearly visible, no competing visuals. "
                    "Cinematic framing, aspirational atmosphere, premium quality. "
                    "No text overlays, no watermarks. Save as desktop.png in the current directory.");
                }
            }
        }
        return val;
    }

    QString defaultMobilePrompt(int step) const override
    {
        QSettings s;
        QString val = readWorkflowSetting(s, id(), categoryKey(), QStringLiteral("step%1_mobile").arg(step));
        if (val.isEmpty()) {
            if (isShoeCategory()) {
                switch (step) {
                case 0: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ mobile image (600x600 px, square) "
                    "showcasing all color variants of the shoe together: %2. "
                    "Clean flat-lay or minimal surface, shoes are the hero — no models, no props. "
                    "Aspirational, premium quality. No text overlays. "
                    "Save as mobile.png in the current directory.");
                case 1: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ mobile image (600x600 px, square) "
                    "of the shoe in %1. Show two complementary angles — "
                    "a 3/4 front view and a side profile highlighting the heel silhouette. "
                    "Clean background, shoe is the hero, aspirational. No text overlays. "
                    "Save as mobile.png in the current directory.");
                case 2: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ mobile image (600x600 px, square) "
                    "featuring a prominent close-up of a key design detail of the shoe%1 — "
                    "heel architecture, upper material texture, hardware or toe shape. "
                    "%2 "
                    "Convey craftsmanship and premium quality. No text overlays. "
                    "Save as mobile.png in the current directory.");
                case 3: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ mobile image (600x600 px, square) "
                    "that evokes the desire to wear this shoe in %1. "
                    "The shoe is the sole subject — no models, no faces. "
                    "Choose a setting that matches its color story: "
                    "rose gold / blush → marble or gallery floor at dusk; "
                    "black → cobblestone or polished dark floor at night; "
                    "white / ivory → sun-lit stone terrace; "
                    "metallic → hotel marble or ballroom floor; "
                    "nude / tan → boutique or salon floor. "
                    "Dramatic ambient lighting, shallow depth of field, editorial framing. "
                    "Premium, cinematic — like the opening shot of a designer fragrance advertisement. "
                    "No text overlays. Save as mobile.png in the current directory.");
                }
            } else {
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
                case 3: return QCoreApplication::translate("APlusWorkflow",
                    "Generate a professional Amazon A+ mobile lifestyle image (600x600 px, square) "
                    "that captures the aspiration of wearing this %1 garment. "
                    "One model in a setting that elevates the product: outdoor, urban, hotel, "
                    "gallery or similar — chosen to match the garment's occasion and mood. "
                    "The garment is the clear hero, editorial framing, aspirational atmosphere. "
                    "No text overlays. Save as mobile.png in the current directory.");
                }
            }
        }
        return val;
    }

    int versionCount(int step) const override
    {
        QSettings s;
        const QString key = QStringLiteral("step%1_versionCount").arg(step);
        const QString catKey = workflowSettingKey(id(), categoryKey(), key);
        if (s.contains(catKey)) return s.value(catKey, 1).toInt();
        return s.value(workflowSettingKey(id(), {}, key), 1).toInt();
    }

    void setDefaultDesktopPrompt(int step, const QString &prompt) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), categoryKey(), QStringLiteral("step%1_desktop").arg(step)), prompt);
    }

    void setDefaultMobilePrompt(int step, const QString &prompt) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), categoryKey(), QStringLiteral("step%1_mobile").arg(step)), prompt);
    }

    void setVersionCount(int step, int count) override
    {
        QSettings s;
        s.setValue(workflowSettingKey(id(), categoryKey(), QStringLiteral("step%1_versionCount").arg(step)), count);
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

        // 3. Aspirational / Cinematic — for any single-color product
        if (orderedColors.size() == 1) {
            ImageSlotSpec spec;
            spec.elementId   = QStringLiteral("image_aspirational");
            spec.displayName = QCoreApplication::translate("APlusWorkflow", "Aspirational Scene");

            const QString color    = orderedColors.first();
            const QString aspInstr = stepInstructions.value(3);

            spec.desktopPrompt = buildPreamble(productDesc, mainImageHint, aspInstr)
                               + defaultDesktopPrompt(3).arg(color);
            spec.mobilePrompt  = buildPreamble(productDesc, mainImageHint, aspInstr)
                               + defaultMobilePrompt(3).arg(color);
            spec.versionCount  = versionCount(3);
            result << spec;
        }

        // 4. Detail / Fabric shot — one per color (mirrors per-color image slots)
        for (const QString &color : orderedColors) {
            ImageSlotSpec spec;
            spec.elementId   = QStringLiteral("image_detail_") + colorSafeId(color);
            spec.displayName = QCoreApplication::translate("APlusWorkflow", "Detail / Fabric — %1").arg(color);

            const QString colorMention =
                QCoreApplication::translate("APlusWorkflow", " in %1").arg(color);

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
            const QString mobileSpec  = defaultMobilePrompt(2).arg(colorMention, fabricAccuracy);

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
