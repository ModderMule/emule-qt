#include "pch.h"
/// @file BugReportDialog.cpp
/// @brief "Submit Bug Report" dialog implementation.

#include "dialogs/BugReportDialog.h"

#include "controls/LogWidget.h"
#include "prefs/Preferences.h"
#include "app/AppConfig.h"
#include "net/HttpDefaults.h"

#include "utils/CrashHandler.h"
#include "utils/DialogSizing.h"

#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QUuid>
#include <QVBoxLayout>

namespace eMule {

static constexpr auto kBugReportApiKey = "tKBLaiRDQ8QA5Sp5HxSfpA2zo4xdI9Zr"; // live tKBLaiRDQ8QA5Sp5HxSfpA2zo4xdI9Zr // local 1YwgLj72qVYs71CGSFFUWmSihF1zYcxb
static constexpr int kMaxLogChars = 60000; // MySQL TEXT column limit is 65,535 bytes; leave margin for UTF-8
static constexpr auto kBaseUrl = "https://emule-qt.org";
static constexpr auto kBugReportWebUrl = "https://emule-qt.org/submit-bug-report/";

BugReportDialog::BugReportDialog(LogWidget* logWidget, QWidget* parent)
    : QDialog(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_logWidget(logWidget)
{
    setWindowTitle(tr("Submit Bug Report"));

    auto* mainLayout = new QVBoxLayout(this);

    // Report Details group
    auto* detailsGroup = new QGroupBox(tr("Report Details"), this);
    auto* detailsLayout = new QFormLayout(detailsGroup);
    detailsLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(tr("Bug Report"));
    m_typeCombo->addItem(tr("Feature Request"));
    detailsLayout->addRow(tr("Type:"), m_typeCombo);

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText(tr("Brief summary of the issue"));
    detailsLayout->addRow(tr("Title:"), m_titleEdit);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("(optional)"));
    detailsLayout->addRow(tr("Name:"), m_nameEdit);

    m_emailEdit = new QLineEdit(this);
    m_emailEdit->setPlaceholderText(tr("(optional)"));
    detailsLayout->addRow(tr("Email:"), m_emailEdit);

    mainLayout->addWidget(detailsGroup);

    // Description group
    auto* descGroup = new QGroupBox(tr("Description"), this);
    auto* descLayout = new QVBoxLayout(descGroup);
    m_descriptionEdit = new QTextEdit(this);
    m_descriptionEdit->setPlaceholderText(tr("Describe the issue in detail..."));
    descLayout->addWidget(m_descriptionEdit);
    mainLayout->addWidget(descGroup, 1); // stretch

    // Attachments group
    auto* attachGroup = new QGroupBox(tr("Attachments"), this);
    auto* attachLayout = new QVBoxLayout(attachGroup);

    auto* screenshotLabel = new QLabel(tr("Screenshots:"), this);
    attachLayout->addWidget(screenshotLabel);

    m_screenshotList = new QListWidget(this);
    m_screenshotList->setMaximumHeight(80);
    attachLayout->addWidget(m_screenshotList);

    auto* screenshotBtnLayout = new QHBoxLayout;
    auto* addScreenBtn = new QPushButton(tr("Add..."), this);
    auto* removeScreenBtn = new QPushButton(tr("Remove"), this);
    screenshotBtnLayout->addWidget(addScreenBtn);
    screenshotBtnLayout->addWidget(removeScreenBtn);
    screenshotBtnLayout->addStretch();
    attachLayout->addLayout(screenshotBtnLayout);

    auto* crashDumpLayout = new QHBoxLayout;
    crashDumpLayout->addWidget(new QLabel(tr("Crash Dump:"), this));
    m_crashDumpEdit = new QLineEdit(this);
    m_crashDumpEdit->setReadOnly(true);
    crashDumpLayout->addWidget(m_crashDumpEdit, 1);
    auto* browseBtn = new QPushButton(tr("Browse..."), this);
    crashDumpLayout->addWidget(browseBtn);
    auto* removeDumpBtn = new QPushButton(tr("Remove"), this);
    crashDumpLayout->addWidget(removeDumpBtn);
    attachLayout->addLayout(crashDumpLayout);

    mainLayout->addWidget(attachGroup);

    // Status label (hidden initially)
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    mainLayout->addWidget(m_statusLabel);

    // Progress bar (hidden initially)
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0); // indeterminate
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

    // Web link alternative
    auto* webLink = new QLabel(
        tr("Alternatively, you can submit bug reports at "
           "<a href=\"%1\">emule-qt.org/submit-bug-report</a>").arg(QLatin1StringView(kBugReportWebUrl)),
        this);
    webLink->setOpenExternalLinks(true);
    mainLayout->addWidget(webLink);

    // Buttons
    auto* buttonBox = new QDialogButtonBox(this);
    m_submitBtn = buttonBox->addButton(tr("Submit"), QDialogButtonBox::AcceptRole);
    buttonBox->addButton(QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    connect(m_submitBtn, &QPushButton::clicked, this, &BugReportDialog::onSubmitClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(addScreenBtn, &QPushButton::clicked, this, &BugReportDialog::onAddScreenshots);
    connect(removeScreenBtn, &QPushButton::clicked, this, &BugReportDialog::onRemoveScreenshot);
    connect(browseBtn, &QPushButton::clicked, this, &BugReportDialog::onBrowseCrashDump);
    connect(removeDumpBtn, &QPushButton::clicked, this, &BugReportDialog::onRemoveCrashDump);

    prefillCrashDump();

    m_titleEdit->setFocus();

    DialogSizing::applySize(this, QSize(500, 560), QSize(540, 620),
                            DialogSizing::Fit::Layout);
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

/// Truncate text to at most kMaxLogChars, cutting at the previous line boundary
/// so the result starts at the beginning of a line (keeps the most recent tail).
static QString truncateLog(const QString& text)
{
    if (text.size() <= kMaxLogChars)
        return text;
    const QString tail = text.right(kMaxLogChars);
    const qsizetype nl = tail.indexOf(u'\n');
    if (nl >= 0 && nl < tail.size() - 1)
        return tail.mid(nl + 1);
    return tail;
}

void BugReportDialog::onSubmitClicked()
{
    const QString title = m_titleEdit->text().trimmed();
    const QString description = m_descriptionEdit->toPlainText().trimmed();

    if (title.isEmpty() || description.isEmpty()) {
        QMessageBox::warning(this, tr("Submit Bug Report"),
                             tr("Please fill in both the title and description fields."));
        if (title.isEmpty())
            m_titleEdit->setFocus();
        else
            m_descriptionEdit->setFocus();
        return;
    }

    m_submitBtn->setEnabled(false);
    m_statusLabel->hide();
    m_progressBar->show();

    // Ensure appToken exists
    QString token = thePrefs.appToken();
    if (token.isEmpty()) {
        token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        thePrefs.setAppToken(token);
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // Helper to add a text part
    auto addText = [multiPart](const char* name, const QString& value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(QLatin1StringView(name)));
        part.setBody(value.toUtf8());
        multiPart->append(part);
    };

    addText("type", m_typeCombo->currentIndex() == 0 ? QStringLiteral("bug") : QStringLiteral("feature"));
    addText("title", title);
    addText("description", description);
    addText("name", m_nameEdit->text().trimmed());
    addText("email", m_emailEdit->text().trimmed());
    addText("userToken", token);

    // App config
    const QString configPath = AppConfig::configDir() + QStringLiteral("/preferences.yml");
    QFile configFile(configPath);
    if (configFile.open(QIODevice::ReadOnly)) {
        addText("appConfig", QString::fromUtf8(configFile.readAll()));
        configFile.close();
    }

    // Log texts – send as file uploads so the server receives them via $_FILES
    auto addLogFile = [multiPart](const char* fieldName, const char* fileName, const QString& text) {
        if (text.isEmpty())
            return;
        auto* buf = new QBuffer(multiPart);
        buf->setData(text.toUtf8());
        buf->open(QIODevice::ReadOnly);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"")
                           .arg(QLatin1StringView(fieldName), QLatin1StringView(fileName)));
        part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain; charset=utf-8"));
        part.setBodyDevice(buf);
        multiPart->append(part);
    };
    if (m_logWidget) {
        addLogFile("logfile",     "logfile.txt",     truncateLog(m_logWidget->logText()));
        addLogFile("logVerbose",  "logVerbose.txt",  truncateLog(m_logWidget->verboseText()));
        addLogFile("logKad",      "logKad.txt",      truncateLog(m_logWidget->kadText()));
    }

    // Screenshot files
    for (int i = 0; i < m_screenshotList->count(); ++i) {
        const QString path = m_screenshotList->item(i)->data(Qt::UserRole).toString();
        auto* file = new QFile(path, multiPart);
        if (!file->open(QIODevice::ReadOnly))
            continue;
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"screens[]\"; filename=\"%1\"")
                           .arg(QFileInfo(path).fileName()));
        const QByteArray mime = m_screenshotList->item(i)->data(Qt::UserRole + 1).toByteArray();
        part.setHeader(QNetworkRequest::ContentTypeHeader,
                       mime.isEmpty() ? QStringLiteral("application/octet-stream") : QString::fromLatin1(mime));
        part.setBodyDevice(file);
        multiPart->append(part);
    }

    // Crash dump file
    if (!m_crashDumpEdit->text().isEmpty()) {
        const QString path = m_crashDumpEdit->text();
        auto* file = new QFile(path, multiPart);
        if (file->open(QIODevice::ReadOnly)) {
            QHttpPart part;
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"crashDump\"; filename=\"%1\"")
                               .arg(QFileInfo(path).fileName()));
            part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
            part.setBodyDevice(file);
            multiPart->append(part);
        }
    }

    const QString apiKey = thePrefs.bugReportApiKey().isEmpty()
                               ? QLatin1StringView(kBugReportApiKey) : thePrefs.bugReportApiKey();
    const QString baseUrl = thePrefs.bugReportDomain().isEmpty()
                                ? QString::fromLatin1(kBaseUrl) : thePrefs.bugReportDomain();

    QNetworkRequest req = eMule::Http::makeRequest(
        QUrl(QStringLiteral("%1/wp-json/emqt/v1/report").arg(baseUrl)));
    req.setRawHeader("X-Api-Key", apiKey.toUtf8());

    req.setTransferTimeout(30000); // 30 seconds
    QNetworkReply* reply = m_nam->post(req, multiPart);
    multiPart->setParent(reply); // ensure cleanup

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_progressBar->hide();

        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            const QJsonObject root = doc.object();
            const int reportId = root.value(QStringLiteral("data")).toObject()
                                      .value(QStringLiteral("reportId")).toInt();

            QString msg = tr("Bug report submitted successfully.");
            if (reportId > 0)
                msg += QStringLiteral("\n\nReport ID: %1").arg(reportId);

            QMessageBox::information(this, tr("Submit Bug Report"), msg);
            accept();
        } else {
            const QByteArray data = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            const QJsonObject root = doc.object();

            // Server app errors use "errorMsg", WordPress native errors use "message"
            QString errorMsg = root.value(QStringLiteral("errorMsg")).toString();
            if (errorMsg.isEmpty())
                errorMsg = root.value(QStringLiteral("message")).toString();
            if (errorMsg.isEmpty())
                errorMsg = reply->errorString();

            m_statusLabel->setText(tr("Error: %1").arg(errorMsg));
            m_statusLabel->show();
            m_submitBtn->setEnabled(true);
        }
    });
}

void BugReportDialog::onAddScreenshots()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select Screenshots"), {},
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));

    QStringList rejected;
    for (const QString& path : files) {
        const QByteArray fmt = QImageReader::imageFormat(path);
        if (fmt.isEmpty()) {
            rejected << QFileInfo(path).fileName();
            continue;
        }
        auto* item = new QListWidgetItem(QFileInfo(path).fileName(), m_screenshotList);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 1, QByteArray("image/" + fmt));
    }
    if (!rejected.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Files"),
                             tr("The following files are not valid images and were skipped:\n%1")
                                 .arg(rejected.join(QStringLiteral(", "))));
    }
}

void BugReportDialog::onRemoveScreenshot()
{
    delete m_screenshotList->takeItem(m_screenshotList->currentRow());
}

void BugReportDialog::onRemoveCrashDump()
{
    m_crashDumpEdit->clear();
}

void BugReportDialog::onBrowseCrashDump()
{
    const QString startDir = CrashHandler::crashDir();
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select Crash Dump"), startDir,
        tr("Dump Files (*.dmp *.crash *.txt);;All Files (*)"));
    if (!file.isEmpty())
        m_crashDumpEdit->setText(file);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void BugReportDialog::prefillCrashDump()
{
    const QString dir = CrashHandler::crashDir();
    if (dir.isEmpty())
        return;

    QDir crashDir(dir);
    if (!crashDir.exists())
        return;

    const QStringList filters{QStringLiteral("*.crash"), QStringLiteral("*.dmp")};
    QFileInfoList entries = crashDir.entryInfoList(filters, QDir::Files, QDir::Time);
    if (!entries.isEmpty())
        m_crashDumpEdit->setText(entries.first().absoluteFilePath());
}

} // namespace eMule
