#pragma once

/// @file Ed3kRoomList.h
/// @brief The "Rooms (ed3k)" tab of the IRC panel, next to "Channels": the ed3k rooms the
/// user is in (GetEd3kRooms), with Join by link, Create, Leave, Copy link and the room
/// actions of the user's role. It only asks; the panel does the work (IrcPanelEd3k.cpp).

#include <QCborArray>
#include <QWidget>

class QPushButton;
class QTreeWidget;

namespace eMule {

class Ed3kRoomList : public QWidget {
    Q_OBJECT

public:
    enum class Action {
        Open,           ///< show the room's tab
        Join,           ///< join by a link
        Create,
        Leave,
        CopyLink,
        CopyMemberKey,  ///< what a newcomer of a closed room sends a moderator
        ImportAdmit,
        AdmitKey,       ///< moderator: a pasted member key
        NewGeneration,  ///< moderator of a closed room
        EditInfo,       ///< owner
        ChangeNick,
    };

    explicit Ed3kRoomList(QWidget* parent = nullptr);

    /// The rows of GetEd3kRooms.
    void setRooms(const QCborArray& rooms);
    [[nodiscard]] const QCborArray& rooms() const { return m_rooms; }

signals:
    /// @p room hex; empty for Join and Create.
    void actionRequested(Ed3kRoomList::Action action, const QString& room);

private:
    void showMenu(const QPoint& pos);
    [[nodiscard]] QString selectedRoom() const;

    QTreeWidget* m_tree = nullptr;
    QCborArray m_rooms;
};

} // namespace eMule
