import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 960
    height: 700
    minimumWidth: 620
    minimumHeight: 440
    visible: true
    title: qsTr("SIP-01 Search")
    // The window colour is the theme's, lifted enough to read as a window
    // rather than as a hole in the desktop. Panels sit one step above it.
    color: palette.windowColor

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false

    // The theme's own colours, with a fallback for each. An app that only
    // takes the accent ends up with a grey interior that clashes with the
    // desktop; these are the tokens Omarchy themes actually define.
    //
    // The map is read with index syntax: a QVariantMap is exposed to QML as
    // a JS object, and `colors.background` does not resolve on it.
    //
    // `muted` is the theme's own readable tone, not its raw `muted` token:
    // a theme's muted can be too dark against its own background, and
    // unreadable text is not worth theme fidelity.
    //
    // `surface` is the panel colour: one step off the window so panels and
    // fields read as raised rather than as holes.
    QtObject {
        id: palette
        readonly property string background: theme.colors["background"] || "#101014"
        readonly property string darkBackground: theme.colors["dark_background"] || "#0b0b0e"
        readonly property string lighterBackground: theme.colors["lighter_background"] || "#17171c"
        readonly property string foreground: theme.colors["foreground"] || "#e6e6e9"
        readonly property string lightForeground: theme.colors["light_foreground"] || "#b8b8c2"
        readonly property string brightForeground: theme.colors["bright_foreground"] || "#f4f4f7"
        readonly property string muted: theme.readableMuted
        readonly property string selection: theme.colors["selection"] || "#1e1e24"
        readonly property string accent: theme.accent
        readonly property string red: theme.colors["red"] || "#ff6b6b"
        readonly property string green: theme.colors["green"] || "#39ff14"
        readonly property string blue: theme.colors["blue"] || "#6b9bc5"
        readonly property string cyan: theme.colors["cyan"] || "#5ec5c5"
        readonly property string yellow: theme.colors["yellow"] || "#e8c96a"
        readonly property string magenta: theme.colors["magenta"] || "#c58ad0"

        // The panel colour and the window colour come from the theme
        // reader, which does the colour maths in C++ where it can. The
        // fallback is only for a theme with no colours at all.
        readonly property string windowColor: theme.windowColor
        readonly property string surface: theme.surfaceColor
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; onActivated: backend.refresh() }
    Shortcut { sequence: "Ctrl+F"; context: Qt.ApplicationShortcut; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "Ctrl+W"; context: Qt.ApplicationShortcut; onActivated: backend.setWebEnabled(!backend.webEnabled) }
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
            anchors.margins: 16
            spacing: 10

            // The header: what this is, and where the search looks.
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("SIP-01 Search")
                        color: palette.brightForeground
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.6
                    }

                    Text {
                        text: qsTr("the decentralized web index, and the web beyond it")
                        color: palette.lightForeground
                        font.pixelSize: 12
                    }
                }

                // Index only, or index plus the clearnet engines.
                Rectangle {
                    implicitWidth: webToggle.width + 4
                    implicitHeight: 30
                    radius: Math.min(8, theme.radius)
                    color: palette.surface
                    border.width: 1
                    border.color: palette.muted

                    RowLayout {
                        id: webToggle
                        anchors.fill: parent
                        spacing: 0

                        // Index only, or index plus the clearnet engines.
                        Rectangle {
                            width: indexLabel.implicitWidth + 22
                            height: parent.height
                            color: !backend.webEnabled ? palette.accent : "transparent"
                            radius: Math.min(8, theme.radius)

                            Text {
                                id: indexLabel
                                anchors.centerIn: parent
                                text: qsTr("Index")
                                color: !backend.webEnabled ? theme.accentForeground : palette.lightForeground
                                font.pixelSize: 11
                                font.weight: !backend.webEnabled ? Font.DemiBold : Font.Normal
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.setWebEnabled(false)
                            }
                        }

                        Rectangle {
                            width: webLabel.implicitWidth + 22
                            height: parent.height
                            color: backend.webEnabled ? palette.accent : "transparent"
                            radius: Math.min(8, theme.radius)

                            Text {
                                id: webLabel
                                anchors.centerIn: parent
                                text: qsTr("Index + Web")
                                color: backend.webEnabled ? theme.accentForeground : palette.lightForeground
                                font.pixelSize: 11
                                font.weight: backend.webEnabled ? Font.DemiBold : Font.Normal
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.setWebEnabled(true)
                            }
                        }
                    }
                }
            }

            // The search field: the whole app starts here.
            Rectangle {
                id: fieldBox
                Layout.fillWidth: true
                implicitHeight: 52
                radius: Math.min(10, theme.radius)
                color: palette.surface
                border.width: 1
                border.color: searchField.activeFocus ? palette.accent : palette.muted

                Behavior on border.color {
                    ColorAnimation { duration: 120 }
                }

                // The search glyph, from the Nerd Font.
                Text {
                    id: searchIcon
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "󰍉"
                    color: searchField.activeFocus ? palette.accent : palette.muted
                    font.pixelSize: 17
                }

                TextField {
                    id: searchField
                    anchors.left: searchIcon.right
                    anchors.right: clearButton.visible ? clearButton.left : parent.right
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    implicitHeight: 50
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: qsTr("Search the decentralized web…")
                    // The field is the source of truth for what the user typed.
                    // A live binding back to the backend would rewrite the text
                    // under the cursor on every keystroke, which drops the last
                    // character. The backend pushes its query in once, at
                    // startup, and after that only the user edits this field.
                    text: backend.query
                    onTextEdited: backend.setQuery(text)
                    color: palette.foreground
                    placeholderTextColor: palette.muted
                    font.pixelSize: 17
                    selectByMouse: true
                    background: null

                    Keys.onDownPressed: win.moveSelection(1)
                    Keys.onUpPressed: win.moveSelection(-1)
                    Keys.onReturnPressed: backend.openSelected()
                    Keys.onEscapePressed: searchField.text = ""
                }

                // The clear button, only when there is something to clear.
                Text {
                    id: clearButton
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    visible: searchField.text.length > 0
                    text: "✕"
                    color: clearHover.hovered ? palette.foreground : palette.muted
                    font.pixelSize: 13

                    HoverHandler { id: clearHover }
                    TapHandler {
                        onTapped: {
                            searchField.text = "";
                            backend.setQuery("");
                            searchField.forceActiveFocus();
                        }
                    }
                }
            }

            // The instant answer: shown when the query is answered outright
            // rather than searched for.
            Rectangle {
                Layout.fillWidth: true
                visible: backend.instantAnswer.title !== undefined
                         && backend.instantAnswer.title !== ""
                height: visible ? 62 : 0
                radius: Math.min(10, theme.radius)
                color: palette.surface
                border.width: 1
                border.color: palette.accent

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 14

                    Text {
                        text: backend.instantAnswer.title || ""
                        color: palette.brightForeground
                        font.pixelSize: 24
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        text: backend.instantAnswer.detail || ""
                        color: palette.muted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }

            // The filter chips: the structured part of the query, shown so
            // the user can see what is being enforced and drop one.
            Flow {
                Layout.fillWidth: true
                visible: backend.queryChips.length > 0
                spacing: 6

                Repeater {
                    model: backend.queryChips

                    delegate: Rectangle {
                        required property var modelData

                        width: label.implicitWidth + 26
                        height: 24
                        radius: 12
                        color: modelData.negated ? Qt.rgba(1, 0.42, 0.49, 0.16) : palette.selection
                        border.width: 1
                        border.color: modelData.negated ? palette.red : palette.muted

                        Text {
                            id: label
                            anchors.centerIn: parent
                            text: (modelData.negated ? "−" : "") + modelData.field + ":" + modelData.value
                            color: modelData.negated ? palette.red : palette.lightForeground
                            font.pixelSize: 11
                        }

                        TapHandler {
                            onTapped: backend.removeFilter(modelData.field, modelData.value)
                        }

                        ToolTip.visible: chipHover.hovered
                        ToolTip.text: qsTr("Remove this filter")
                        HoverHandler { id: chipHover }
                    }
                }
            }

            // The status line: what the index holds and where it came from.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: backend.status
                    color: palette.muted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                // Which engines answered. A provider that failed is not
                // hidden — a silent gap looks like "nothing matched".
                Repeater {
                    model: backend.engines

                    delegate: Rectangle {
                        required property var modelData

                        width: engineLabel.implicitWidth + 16
                        height: 19
                        radius: 9
                        color: palette.selection
                        border.width: 1
                        border.color: modelData.ok && modelData.count > 0
                                     ? palette.muted : Qt.rgba(1, 0.42, 0.49, 0.5)

                        Text {
                            id: engineLabel
                            anchors.centerIn: parent
                            text: modelData.id + (modelData.count > 0 ? " " + modelData.count : "")
                            color: modelData.ok && modelData.count > 0
                                   ? palette.lightForeground : palette.muted
                            font.pixelSize: 10
                        }
                    }
                }

                BusyIndicator {
                    visible: backend.busy || backend.searching
                    running: backend.busy || backend.searching
                    implicitWidth: 16
                    implicitHeight: 16
                }

                Text {
                    visible: backend.busy || backend.searching
                    text: backend.busy ? qsTr("indexing…") : qsTr("searching…")
                    color: palette.muted
                    font.pixelSize: 11
                }
            }

            // The results.
            ListView {
                id: resultList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.results
                spacing: 1
                highlightMoveDuration: 120
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: delegate
                    width: ListView.view.width
                    height: row.implicitHeight + 18

                    required property int index
                    required property var modelData

                    readonly property bool current: backend.selected === index
                    readonly property bool fromWeb: modelData.web === true

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 2
                        anchors.rightMargin: 2
                        radius: Math.min(8, theme.radius)
                        color: current ? palette.selection : "transparent"

                        // The accent bar, rather than a border around the
                        // whole row — it marks the selection without drawing
                        // a box around the text.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: parent.height - 16
                            radius: 1
                            color: palette.accent
                            visible: delegate.current
                        }
                    }

                    RowLayout {
                        id: row
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 14
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                text: delegate.modelData.title
                                color: delegate.current ? palette.brightForeground : palette.foreground
                                font.pixelSize: 15
                                font.weight: delegate.current ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: delegate.modelData.url
                                color: delegate.current ? palette.accent : palette.muted
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            Text {
                                visible: delegate.modelData.description !== ""
                                text: delegate.modelData.description
                                color: palette.muted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                maximumLineCount: 2
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        // Where the result came from: the engine name for a
                        // web hit, the indexer's agreement count for an
                        // index hit. An index result always shows a badge,
                        // even with a single indexer — otherwise the two
                        // halves of the search are indistinguishable.
                        //
                        // The index badge is filled with the accent and its
                        // text is the accent's foreground, because an
                        // outlined accent badge on a near-black surface is
                        // too faint at 10 pixels.
                        Rectangle {
                            visible: delegate.fromWeb
                                     || delegate.modelData.indexers > 0
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 2
                            implicitWidth: badge.implicitWidth + 14
                            implicitHeight: 20
                            radius: 10
                            color: delegate.fromWeb ? palette.selection : palette.accent
                            border.width: 1
                            border.color: delegate.fromWeb ? palette.muted : palette.accent

                            Text {
                                id: badge
                                anchors.centerIn: parent
                                text: delegate.fromWeb
                                      ? (delegate.modelData.engine || "web")
                                      : qsTr("index %1×").arg(delegate.modelData.indexers)
                                color: delegate.fromWeb ? palette.lightForeground : theme.accentForeground
                                font.pixelSize: 10
                                font.weight: delegate.fromWeb ? Font.Normal : Font.DemiBold
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
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: backend.results.length === 0 && backend.query !== ""
                      && !backend.busy && !backend.searching
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: qsTr("No results for “%1”").arg(backend.query)
                    color: palette.lightForeground
                    horizontalAlignment: Qt.AlignHCenter
                    font.pixelSize: 15
                }

                Text {
                    Layout.fillWidth: true
                    text: backend.webEnabled
                        ? qsTr("The index is young. Try the web engines, or a broader word.")
                        : qsTr("Turn on the web engines to search beyond the index.")
                    color: palette.muted
                    horizontalAlignment: Qt.AlignHCenter
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            // The shortcut overlay.
            Rectangle {
                Layout.fillWidth: true
                visible: win.helpVisible
                height: visible ? 118 : 0
                color: palette.surface
                radius: Math.min(8, theme.radius)
                border.width: 1
                border.color: palette.muted

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    columns: 2
                    columnSpacing: 24
                    rowSpacing: 5

                    Text { text: qsTr("↓ ↑"); color: palette.accent; font.pixelSize: 11 }
                    Text { text: qsTr("Move through results"); color: palette.muted; font.pixelSize: 11 }
                    Text { text: qsTr("Enter"); color: palette.accent; font.pixelSize: 11 }
                    Text { text: qsTr("Open the selected page"); color: palette.muted; font.pixelSize: 11 }
                    Text { text: qsTr("Ctrl+W"); color: palette.accent; font.pixelSize: 11 }
                    Text { text: qsTr("Search the web engines too"); color: palette.muted; font.pixelSize: 11 }
                    Text { text: qsTr("Ctrl+R"); color: palette.accent; font.pixelSize: 11 }
                    Text { text: qsTr("Refresh the index"); color: palette.muted; font.pixelSize: 11 }
                    Text { text: qsTr("Ctrl+C"); color: palette.accent; font.pixelSize: 11 }
                    Text { text: qsTr("Copy the selected link"); color: palette.muted; font.pixelSize: 11 }
                    Text { text: qsTr("?"); color: palette.accent; font.pixelSize: 11 }
                    Text { text: qsTr("Show or hide this help"); color: palette.muted; font.pixelSize: 11 }
                    Text { text: qsTr("Q"); color: palette.accent; font.pixelSize: 11 }
                    Text { text: qsTr("Quit"); color: palette.muted; font.pixelSize: 11 }
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
