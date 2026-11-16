import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 900
    height: 660
    minimumWidth: 560
    minimumHeight: 420
    visible: true
    title: qsTr("SIP-01 Search")
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; onActivated: backend.refresh() }
    Shortcut { sequence: "Ctrl+F"; context: Qt.ApplicationShortcut; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; onActivated: win.moveSelection(1) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; onActivated: win.moveSelection(-1) }
    Shortcut { sequence: "Return"; context: Qt.ApplicationShortcut; onActivated: backend.openSelected() }
    Shortcut { sequence: "Ctrl+C"; context: Qt.ApplicationShortcut; onActivated: backend.copyUrl(backend.selected) }

    function moveSelection(delta) {
        const count = backend.results.length;
        if (count === 0)
            return;
        let next = backend.selected + delta;
        if (next < 0)
            next = 0;
        if (next >= count)
            next = count - 1;
        backend.selected = next;
        resultList.positionViewAtIndex(next, ListView.Contain);
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            // The search field: the whole app starts here.
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search the decentralized web…")
                // The field is the source of truth for what the user typed.
                // A live binding back to the backend would rewrite the text
                // under the cursor on every keystroke, which drops the last
                // character. The backend pushes its query in once, at
                // startup, and after that only the user edits this field.
                text: backend.query
                onTextEdited: backend.setQuery(text)
                color: "#e6e6e9"
                placeholderTextColor: "#6b6b76"
                font.pixelSize: 17
                selectByMouse: true
                leftPadding: 14
                rightPadding: 14
                topPadding: 10
                bottomPadding: 10
                background: Rectangle {
                    implicitHeight: 48
                    radius: Math.min(8, theme.radius)
                    color: "#18181c"
                    border.width: 1
                    border.color: searchField.activeFocus ? theme.accent : "#2a2a31"
                }

                Keys.onDownPressed: win.moveSelection(1)
                Keys.onUpPressed: win.moveSelection(-1)
                Keys.onReturnPressed: backend.openSelected()
                Keys.onEscapePressed: searchField.text = ""
            }

            // The status line: what the index holds and where it came from.
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: backend.status
                    color: "#8b8b96"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                BusyIndicator {
                    visible: backend.busy
                    running: backend.busy
                    implicitWidth: 18
                    implicitHeight: 18
                }

                Button {
                    text: qsTr("Refresh index")
                    onClicked: backend.refresh()
                    flat: true
                    Material.roundedScale: Math.min(4, theme.radius)
                    contentItem: Text {
                        text: "Refresh index"
                        color: theme.accent
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // The results.
            ListView {
                id: resultList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.results
                spacing: 2
                highlightMoveDuration: 120
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: delegate
                    width: ListView.view.width
                    height: row.implicitHeight + 16

                    required property int index
                    required property var modelData

                    readonly property bool current: backend.selected === index

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4
                        radius: Math.min(6, theme.radius)
                        color: current ? "#1e1e24" : "transparent"
                        border.width: 1
                        border.color: current ? theme.accent : "transparent"
                    }

                    RowLayout {
                        id: row
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: delegate.modelData.title
                                color: "#f0f0f3"
                                font.pixelSize: 15
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: delegate.modelData.url
                                color: current ? theme.accent : "#6b8fb5"
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            Text {
                                visible: delegate.modelData.description !== ""
                                text: delegate.modelData.description
                                color: "#7c7c88"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                maximumLineCount: 2
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        // How many independent indexers saw this page.
                        Rectangle {
                            visible: delegate.modelData.indexers > 1
                            Layout.alignment: Qt.AlignTop
                            implicitWidth: agreement.width + 12
                            implicitHeight: 22
                            radius: Math.min(11, theme.radius)
                            color: "#22222a"
                            border.width: 1
                            border.color: "#2e2e37"

                            Text {
                                id: agreement
                                anchors.centerIn: parent
                                text: delegate.modelData.indexers + "×"
                                color: "#9a9aa6"
                                font.pixelSize: 11
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            backend.selected = delegate.index;
                            backend.openResult(delegate.index);
                        }
                    }
                }
            }

            // Nothing found: say so plainly rather than showing a blank pane.
            Label {
                Layout.fillWidth: true
                visible: backend.results.length === 0 && backend.query !== ""
                text: qsTr("No results for “%1”").arg(backend.query)
                color: "#7c7c88"
                horizontalAlignment: Qt.AlignHCenter
                font.pixelSize: 14
            }

            // The shortcut overlay.
            Rectangle {
                Layout.fillWidth: true
                visible: win.helpVisible
                height: visible ? 96 : 0
                color: "#16161a"
                radius: Math.min(6, theme.radius)
                border.width: 1
                border.color: "#2a2a31"

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    columns: 2
                    columnSpacing: 24
                    rowSpacing: 4

                    Text { text: qsTr("↓ ↑"); color: theme.accent; font.pixelSize: 12 }
                    Text { text: qsTr("Move through results"); color: "#8b8b96"; font.pixelSize: 12 }
                    Text { text: qsTr("Enter"); color: theme.accent; font.pixelSize: 12 }
                    Text { text: qsTr("Open the selected page"); color: "#8b8b96"; font.pixelSize: 12 }
                    Text { text: qsTr("Ctrl+R"); color: theme.accent; font.pixelSize: 12 }
                    Text { text: qsTr("Refresh the index"); color: "#8b8b96"; font.pixelSize: 12 }
                    Text { text: qsTr("?"); color: theme.accent; font.pixelSize: 12 }
                    Text { text: qsTr("Show or hide this help"); color: "#8b8b96"; font.pixelSize: 12 }
                    Text { text: qsTr("Q"); color: theme.accent; font.pixelSize: 12 }
                    Text { text: qsTr("Quit"); color: "#8b8b96"; font.pixelSize: 12 }
                }
            }
        }
    }

    Component.onCompleted: {
        searchField.forceActiveFocus();
        if (backend.totalDocuments === 0)
            backend.refresh();
    }
}
