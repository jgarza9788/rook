import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Rook.Runtime

// Audio and video in the quick view (built only with Qt Multimedia). Starts
// playing at once; Space pauses, ←/→ (h/l) seek 5 s, Esc closes the preview.
FocusScope {
    id: root

    property url source
    property bool autoPlay: true
    signal closeRequested()

    function seek(ms) {
        player.position = Math.max(0, Math.min(player.duration, player.position + ms));
    }

    function clock(ms) {
        const s = Math.floor(ms / 1000);
        const m = Math.floor(s / 60);
        const h = Math.floor(m / 60);
        const two = n => (n < 10 ? "0" : "") + n;
        return (h > 0 ? h + ":" + two(m % 60) : m) + ":" + two(s % 60);
    }

    focus: true

    MediaPlayer {
        id: player
        source: root.source
        videoOutput: video
        audioOutput: AudioOutput {}
        onSourceChanged: if (root.autoPlay) play()
    }

    Keys.onPressed: event => {
        const vim = Settings.keyboardMode === "vim";
        if (event.key === Qt.Key_Space) {
            player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play();
        } else if (event.key === Qt.Key_Escape) {
            player.stop();
            root.closeRequested();
        } else if (event.key === Qt.Key_Left || (vim && event.key === Qt.Key_H)) {
            root.seek(-5000);
        } else if (event.key === Qt.Key_Right || (vim && event.key === Qt.Key_L)) {
            root.seek(5000);
        } else {
            return; // j/k, Enter, zoom: the quick view's
        }
        event.accepted = true;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            VideoOutput {
                id: video
                anchors.fill: parent
                visible: player.hasVideo
            }

            Text {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                visible: !player.hasVideo
                text: player.error !== MediaPlayer.NoError ? player.errorString : "♪"
                color: player.error !== MediaPlayer.NoError ? Colors.error : Colors.textDim
                font.pixelSize: player.error !== MediaPlayer.NoError ? 13 : 96
            }

            MouseArea {
                anchors.fill: parent
                onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ToolbarButton {
                symbol: player.playbackState === MediaPlayer.PlayingState ? "⏸" : "▶"
                tip: qsTr("Play / pause (Space)")
                onTriggered: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
            }

            Text {
                textFormat: Text.PlainText
                text: root.clock(player.position)
                color: Colors.textDim
                font.pixelSize: 11
                font.family: "monospace"
            }

            Slider {
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, player.duration)
                value: player.position
                enabled: player.seekable
                onMoved: player.position = value
            }

            Text {
                textFormat: Text.PlainText
                text: root.clock(player.duration)
                color: Colors.textDim
                font.pixelSize: 11
                font.family: "monospace"
            }
        }
    }
}
