#pragma once

/// @file HosterCombo.h
/// @brief The site box of the Add account dialogs: an editable combo of the resolver plugins
/// that take an account, with favicons, filtered while typing (JDownloader's Add account
/// dialog). Any domain can be typed; a typed plugin name picks its host.

#include <QAbstractListModel>
#include <QComboBox>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>

class QAction;

namespace eMule {

class HosterIcons;
class IpcClient;

class HosterCombo : public QComboBox {
    Q_OBJECT

public:
    enum class Kind { Hosters, Solvers, MultiHosters };
    /// A service this version knows without the core's list (and its name).
    struct Known {
        QString host;
        QString name;
    };

    HosterCombo(IpcClient* ipc, Kind kind, const QList<Known>& known, QWidget* parent = nullptr);

    /// The domain picked or typed: lower case, no "www."; a typed name gives its host.
    [[nodiscard]] QString host() const;
    void setHost(const QString& host);

signals:
    /// host() changed (picked or typed)
    void hostChanged(const QString& host);

private:
    struct Row {
        QString host;
        QString name;
    };
    class Model : public QAbstractListModel {
    public:
        explicit Model(HosterCombo* combo);
        [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
        [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
        void setRows(QList<Row> rows);
        void iconChanged(const QString& host);
        [[nodiscard]] int rowOf(const QString& host) const { return m_index.value(host, -1); }
        [[nodiscard]] const Row& at(int row) const { return m_rows.at(row); }

    private:
        HosterCombo* m_combo;
        QList<Row> m_rows;
        QHash<QString, int> m_index;
    };

    void rebuild();
    void updateLeadingIcon();

    QPointer<HosterIcons> m_icons;
    Kind m_kind;
    QList<Known> m_known;
    Model* m_model;
    QAction* m_iconAction = nullptr;
};

} // namespace eMule
