import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Dialogs 1.3
import QtQuick.Layouts 1.3

import ComputerModel 1.0

import ComputerManager 1.0
import StreamingPreferences 1.0

GridView {
    property ComputerModel computerModel : createModel()

    id: pcGrid
    anchors.fill: parent
    anchors.leftMargin: (parent.width % (cellWidth + anchors.rightMargin)) / 2
    anchors.topMargin: 20
    anchors.rightMargin: 5
    anchors.bottomMargin: 5
    cellWidth: 350; cellHeight: 350;
    focus: true
    objectName: "Computers"

    // The StackView will trigger a visibility change when
    // we're pushed onto it, causing our onVisibleChanged
    // routine to run, but only if we start as invisible
    visible: false

    StreamingPreferences {
        id: prefs
    }

    Component.onCompleted: {
        // Setup signals on CM
        ComputerManager.computerAddCompleted.connect(addComplete)

        if (!prefs.hasAnyHardwareAcceleration()) {
            noHwDecoderDialog.open()
        }
    }

    onVisibleChanged: {
        if (visible) {
            // Start polling when this view is shown
            ComputerManager.startPolling()
        }
        else {
            // Stop polling when this view is not top-most
            ComputerManager.stopPollingAsync()
        }
    }

    function pairingComplete(error)
    {
        // Close the PIN dialog
        pairDialog.close()

        // Start polling again
        ComputerManager.startPolling()

        // Display a failed dialog if we got an error
        if (error !== null) {
            errorDialog.text = error
            errorDialog.open()
        }
    }

    function addComplete(success)
    {
        if (!success) {
            errorDialog.text = "Unable to connect to the specified PC. Ensure GameStream is enabled in GeForce Experience."
            errorDialog.open()
        }
    }

    function createModel()
    {
        var model = Qt.createQmlObject('import ComputerModel 1.0; ComputerModel {}', parent, '')
        model.initialize(ComputerManager)
        model.pairingCompleted.connect(pairingComplete)
        return model
    }

    model: createModel()

    delegate: Item {
        width: 300; height: 300;

        Image {
            id: pcIcon
            anchors.horizontalCenter: parent.horizontalCenter;
            source: {
                model.addPc ? "qrc:/res/ic_add_to_queue_white_48px.svg" : "qrc:/res/ic_tv_white_48px.svg"
            }
            sourceSize {
                width: 200
                height: 200
            }
        }

        Text {
            id: pcNameText
            text: model.name
            color: "white"

            width: parent.width
            anchors.top: pcIcon.bottom
            font.pointSize: 36
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            function getStatusText(model)
            {
                if (model.online) {
                    var text = "<font color=\"green\">Online</font>"
                    text += "<font color=\"white\"> - </font>"
                    if (model.paired) {
                        text += "<font color=\"skyblue\">Paired</font>"
                    }
                    else if (model.busy) {
                        text += "<font color=\"red\">Busy</font>"
                    }
                    else {
                        text += "<font color=\"red\">Not Paired</font>"
                    }
                    return text
                }
                else {
                    return "<font color=\"red\">Offline</font>";
                }
            }

            id: pcPairedText
            text: getStatusText(model)
            visible: !model.addPc

            width: parent.width
            anchors.top: pcNameText.bottom
            font.pointSize: 24
            horizontalAlignment: Text.AlignHCenter
        }

        Menu {
            id: pcContextMenu
            MenuItem {
                text: "Wake PC"
                onTriggered: computerModel.wakeComputer(index)
                visible: !model.online && model.wakeable
                height: visible ? implicitHeight : 0
            }
            MenuItem {
                text: "Delete PC"
                onTriggered: {
                    deletePcDialog.pcIndex = index
                    // get confirmation first, actual closing is called from the dialog
                    deletePcDialog.open()
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton;
            onClicked: {
                if(mouse.button === Qt.LeftButton) {
                    if (model.addPc) {
                        addPcDialog.open()
                    }
                    else if (model.online) {
                        if (model.paired) {
                            // go to game view
                            var component = Qt.createComponent("AppView.qml")
                            var appView = component.createObject(stackView)
                            appView.computerIndex = index
                            appView.objectName = model.name
                            stackView.push(appView)
                        }
                        else {
                            if (!model.busy) {
                                var pin = ("0000" + Math.floor(Math.random() * 10000)).slice(-4)

                                // Stop polling, since pairing may make GFE unresponsive
                                ComputerManager.stopPollingAsync()

                                // Kick off pairing in the background
                                computerModel.pairComputer(index, pin)

                                // Display the pairing dialog
                                pairDialog.pin = pin
                                pairDialog.open()
                            }
                            else {
                                // cannot pair while something is streaming or attempting to pair
                                errorDialog.text = "This PC is currently busy. Make sure to quit any running games and try again."
                                errorDialog.open()
                            }
                        }
                    } else if(!model.online) {
                        pcContextMenu.open()
                    }

                }
                else { // right click
                    if(!model.addPc) { // but only for actual PCs, not the add-pc option
                        pcContextMenu.open()
                    }
                }
            }
        }
    }

    MessageDialog {
        id: errorDialog
        // don't allow edits to the rest of the window while open
        modality:Qt.WindowModal
        icon: StandardIcon.Critical
        standardButtons: StandardButton.Ok
    }

    MessageDialog {
        id: noHwDecoderDialog
        modality:Qt.WindowModal
        icon: StandardIcon.Warning
        standardButtons: StandardButton.Ok | StandardButton.Help
        text: "No functioning hardware accelerated H.264 video decoder was detected by Moonlight. " +
              "Your streaming performance may be severely degraded in this configuration. " +
              "Click the Help button for more information on solving this problem."
        onHelp: {
            Qt.openUrlExternally("https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems");
        }
    }

    MessageDialog {
        id: pairDialog
        // don't allow edits to the rest of the window while open
        modality:Qt.WindowModal
        property string pin : "0000"
        text:"Please enter " + pin + " on your GameStream PC. This dialog will close when pairing is completed."
        standardButtons: StandardButton.Cancel
        onRejected: {
            // FIXME: We should interrupt pairing here
        }
    }

    MessageDialog {
        id: deletePcDialog
        // don't allow edits to the rest of the window while open
        modality:Qt.WindowModal
        property int pcIndex : -1;
        text:"Are you sure you want to remove this PC?"
        standardButtons: StandardButton.Yes |StandardButton.No
        onYes: {
            console.log("deleting PC pairing for PC at index: " + pcIndex)
            computerModel.deleteComputer(pcIndex);
            // hack to remove the child from the gridview
            model = createModel()
        }
    }

    Dialog {
        id: addPcDialog
        property string label: "Enter the IP address of your GameStream PC"
        property string hint: "192.168.1.100"
        property alias editText : editTextItem

        standardButtons: StandardButton.Ok | StandardButton.Cancel
        onVisibleChanged: {
            editTextItem.focus = true
            editTextItem.selectAll()
        }
        onAccepted: {
            ComputerManager.addNewHost(editText.text, false)
        }

        ColumnLayout {
            Text {
                id: addPcDialogLabel
                text: addPcDialog.label
            }
            Rectangle {
                implicitWidth: parent.parent.width
                height: editTextItem.height

                TextInput {
                    id: editTextItem
                    inputMethodHints: Qt.ImhPreferUppercase
                    text: addPcDialog.hint
                }
            }
        }
    }

    ScrollBar.vertical: ScrollBar {
        parent: pcGrid.parent
        anchors {
            top: pcGrid.top
            left: pcGrid.right
            bottom: pcGrid.bottom

            leftMargin: -10
        }
    }
}
