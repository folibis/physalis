#include "AboutDialog.h"
#include "ui_AboutDialog.h"

#include "EngineRegistry.h"
#include "Icons.h"

#include <QPixmap>
#include <QStringList>

AboutDialog::AboutDialog(const QString &version, QWidget *parent)
    : QDialog(parent), m_ui(new Ui::AboutDialog)
{
    m_ui->setupUi(this);

    // Name in the icon's orange, version in its green.
    m_ui->titleLabel->setTextFormat(Qt::RichText);
    m_ui->titleLabel->setText(
        version.isEmpty()
            ? QStringLiteral("<span style=\"color:#FE540F\">Physalis</span>")
            : QStringLiteral("<span style=\"color:#FE540F\">Physalis</span> "
                             "<span style=\"color:#05C936\">%1</span>").arg(version));
    m_ui->titleLabel->setStyleSheet(
        QStringLiteral("font-size: 18px; font-weight: bold;"));

    // The application icon, large. Rendered from the SVG rather than scaled up
    // from a small pixmap, so it stays sharp.
    m_ui->iconLabel->setPixmap(Icons::app().pixmap(96, 96));

    // A real mailto link, opened by the system mail client.
    m_ui->authorLabel->setTextFormat(Qt::RichText);
    m_ui->authorLabel->setOpenExternalLinks(true);
    m_ui->authorLabel->setText(
        tr("Created by: <a href=\"mailto:%1\">%1</a>")
            .arg(QStringLiteral("ruslan@muhlinin.com")));
    m_ui->authorLabel->setStyleSheet(QStringLiteral("color: #6f6f6f;"));

    // Bold heading with a bulleted, indented list under it -- which is why
    // this label is rich text and left-aligned rather than centred.
    QStringList lines { tr("<b>Plugins:</b>") };
    const auto plugins = physics::EngineRegistry::loadedPlugins();
    const QString bullet =
        QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;%1&nbsp;").arg(QChar(0x2022));
    if (plugins.isEmpty()) {
        lines << bullet + tr("none found");
    } else {
        for (const auto &plugin : plugins) {
            lines << bullet + (plugin.version.isEmpty()
                                   ? plugin.name.toHtmlEscaped()
                                   : QStringLiteral("%1 %2").arg(plugin.name.toHtmlEscaped(),
                                                                 plugin.version.toHtmlEscaped()));
        }
    }
    m_ui->pluginsLabel->setTextFormat(Qt::RichText);
    m_ui->pluginsLabel->setText(lines.join(QStringLiteral("<br>")));

    connect(m_ui->closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

AboutDialog::~AboutDialog() = default;
