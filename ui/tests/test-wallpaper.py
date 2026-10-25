#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
import os
from pathlib import Path
import time

os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["QT_QUICK_BACKEND"] = "software"
from PySide6.QtCore import QCoreApplication, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickItem, QQuickView

app = QGuiApplication([])
view = QQuickView()
view.resize(1120, 700)
view.setResizeMode(QQuickView.SizeRootObjectToView)
view.setSource(QUrl.fromLocalFile(str(Path(__file__).parents[1] / "aurora-fingerprint/contents/ui/main.qml")))
assert view.status() == QQuickView.Ready, [e.toString() for e in view.errors()]
view.show()
for _ in range(20):
    QCoreApplication.processEvents()
    time.sleep(0.01)
root = view.rootObject()
feedback = root.findChild(QQuickItem, "auroraFingerprintFeedback")
assert feedback is not None
assert feedback.parentItem() == view.contentItem(), "Feedback would be blurred with the wallpaper"
assert feedback.property("mode") == "idle", "No fake authentication state should be shown"
assert feedback.property("opacity") == 0
print("PASS: native WallpaperItem loaded; feedback mounted above wallpaper blur; idle is hidden")
