#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Preview only: this application neither authenticates nor captures fingerprints."""
import argparse
import os
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--screenshot", type=Path)
parser.add_argument("--mode", choices=["waiting", "scanning", "success", "failure"], default="waiting")
args = parser.parse_args()
if args.screenshot:
    os.environ["QT_QPA_PLATFORM"] = "offscreen"
    os.environ["QT_QUICK_BACKEND"] = "software"

from PySide6.QtCore import QTimer, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickWindow

app = QGuiApplication(sys.argv[:1])
engine = QQmlApplicationEngine()
engine.load(QUrl.fromLocalFile(str(Path(__file__).with_name("Main.qml"))))
if not engine.rootObjects():
    sys.exit(1)
window = engine.rootObjects()[0]
window.setProperty("previewMode", args.mode)
if args.screenshot:
    def capture():
        try:
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            image = window.grabWindow()
            app.exit(0 if not image.isNull() and image.save(str(args.screenshot)) else 1)
        except Exception as error:
            print(error, file=sys.stderr)
            app.exit(1)
    QTimer.singleShot(900, capture)
sys.exit(app.exec())
