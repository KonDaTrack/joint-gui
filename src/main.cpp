#include <QApplication>
#include <QStyleFactory>
#include "device/JointTypes.h"
#include "ui/MainWindow.h"

// 全局深色工业科技风主题（Dark Industrial Tech）。
// 仅使用原生 Qt 5 QSS，零第三方依赖。
// 色彩系统：
//   全局背景  #1A1D21
//   功能卡片  #242830（6px 圆角，1px #333943 边框）
//   科技强调  #007ACC / #00A8FF
//   实时数据  #00FFC8
//   工业状态  绿 #00C853 / 黄 #FFAB00 / 红 #FF5252
static const char kGlobalQss[] = R"(
/* ============ 全局 ============ */
QWidget {
    background-color: #1A1D21;
    color: #D0D6DD;
    font-size: 14px;
    selection-background-color: #007ACC;
    selection-color: #FFFFFF;
}
QMainWindow { background-color: #1A1D21; }
QWidget#centralRoot { background-color: #1A1D21; }
/* 标签页内部容器透明，露出 QTabWidget::pane 的统一卡片底色 */
QTabWidget QWidget { background: transparent; }

QLabel {
    background: transparent;
    color: #D0D6DD;
}

/* ============ 功能卡片 ============ */
QWidget#PanelCard {
    background-color: #242830;
    border: 1px solid #333943;
    border-radius: 6px;
}

/* ============ 按钮 ============ */
QPushButton {
    background-color: #2A2F37;
    border: 1px solid #3A4046;
    border-radius: 4px;
    padding: 6px 16px;
    color: #E6EAF0;
    min-height: 24px;
}
QPushButton:hover { background-color: #333943; border-color: #007ACC; }
QPushButton:pressed { background-color: #1E2228; }
QPushButton:focus { border-color: #007ACC; }
QPushButton:disabled { color: #6A7280; background-color: #1E2228; border-color: #2A2F37; }

QPushButton#primaryButton {
    background-color: #007ACC;
    border: 1px solid #007ACC;
    color: #FFFFFF;
    font-weight: bold;
}
QPushButton#primaryButton:hover { background-color: #0090E0; border-color: #0090E0; }
QPushButton#primaryButton:pressed { background-color: #0063A6; }
QPushButton#primaryButton:disabled { background-color: #1E2228; color: #6A7280; border-color: #2A2F37; }

QPushButton#warningButton {
    background-color: #2A2F37;
    border: 1px solid #FFAB00;
    color: #FFAB00;
}
QPushButton#warningButton:hover { background-color: #333943; }
QPushButton#warningButton:pressed { background-color: #1E2228; }

QPushButton#dangerButton {
    background-color: #FF5252;
    border: 1px solid #FF5252;
    color: #FFFFFF;
    border-radius: 6px;
    font-size: 20px;
    font-weight: bold;
    min-height: 52px;
}
QPushButton#dangerButton:hover { background-color: #FF6E6E; border-color: #FF8A8A; }
QPushButton#dangerButton:pressed { background-color: #D93025; }

QMessageBox QPushButton { min-width: 80px; }

/* ============ 输入控件 ============ */
QLineEdit {
    background-color: #1A1D21;
    border: 1px solid #333943;
    border-radius: 4px;
    padding: 5px 8px;
    color: #E6EAF0;
    min-height: 22px;
}
QLineEdit:focus { border-color: #007ACC; }
QLineEdit:hover { border-color: #4A525F; }
QLineEdit:disabled { color: #6A7280; background-color: #1E2228; }

QComboBox {
    background-color: #1A1D21;
    border: 1px solid #333943;
    border-radius: 4px;
    padding: 5px 30px 5px 10px;
    color: #E6EAF0;
    min-height: 22px;
}
QComboBox:focus { border-color: #007ACC; }
QComboBox:hover { border-color: #4A525F; }
QComboBox:disabled { color: #6A7280; background-color: #1E2228; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 26px;
    border-left: 1px solid #333943;
    border-top-right-radius: 4px;
    border-bottom-right-radius: 4px;
}
QComboBox::down-arrow {
    image: none;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 6px solid #8A94A0;
    margin-right: 8px;
}
QComboBox QAbstractItemView {
    background-color: #242830;
    color: #D0D6DD;
    border: 1px solid #333943;
    selection-background-color: #007ACC;
    selection-color: #FFFFFF;
    outline: 0;
}

QCheckBox { color: #D0D6DD; spacing: 8px; }
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #3A4046;
    border-radius: 3px;
    background-color: #1A1D21;
}
QCheckBox::indicator:hover { border-color: #007ACC; }
QCheckBox::indicator:checked { background-color: #007ACC; border-color: #007ACC; }
QCheckBox::indicator:disabled { background-color: #1E2228; border-color: #2A2F37; }

/* ============ 标签页 ============ */
QTabWidget::pane {
    border: 1px solid #333943;
    border-radius: 4px;
    top: -1px;
    background-color: #242830;
}
QTabBar::tab {
    background-color: #1E2228;
    color: #8A94A0;
    border: 1px solid #333943;
    border-bottom: none;
    padding: 9px 22px;
    margin-right: 3px;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    font-size: 14px;
}
QTabBar::tab:hover { background-color: #242830; color: #D0D6DD; }
QTabBar::tab:selected {
    background-color: #242830;
    color: #00A8FF;
    border-bottom: 2px solid #00A8FF;
    font-weight: bold;
}

/* ============ 滚动区 / 滚动条 ============ */
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical {
    background: #1A1D21;
    width: 12px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #3A4046;
    border-radius: 6px;
    min-height: 30px;
    margin: 2px;
}
QScrollBar::handle:vertical:hover { background: #4A525F; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; background: none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
QScrollBar:horizontal {
    background: #1A1D21;
    height: 12px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #3A4046;
    border-radius: 6px;
    min-width: 30px;
    margin: 2px;
}
QScrollBar::handle:horizontal:hover { background: #4A525F; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; background: none; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }

/* ============ 分隔条 ============ */
QSplitter::handle { background-color: #1A1D21; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }
QSplitter::handle:hover { background-color: #007ACC; }

/* ============ 状态栏 ============ */
QStatusBar {
    background-color: #1E2228;
    border-top: 1px solid #333943;
    color: #D0D6DD;
}
QStatusBar::item { border: none; }

/* ============ 对话框 / 提示 ============ */
QDialog { background-color: #1A1D21; }
QMessageBox { background-color: #1A1D21; }
QMessageBox QLabel { color: #D0D6DD; }
QDialog QLabel#dialogTitle { font-size: 16px; font-weight: bold; color: #00A8FF; }
QToolTip {
    background-color: #242830;
    color: #D0D6DD;
    border: 1px solid #333943;
    padding: 4px 8px;
}

/* ============ 实时数据大字号 ============ */
QLabel#bigValue {
    color: #00FFC8;
    font-size: 20px;
    font-weight: bold;
    font-family: "DejaVu Sans Mono";
}
QLabel#sectionTitle {
    color: #00A8FF;
    font-weight: bold;
    font-size: 15px;
    margin-top: 4px;
}
)";

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Fusion 内建样式 + QSS，保证深色主题跨控件渲染一致
    if (QStyle* fs = QStyleFactory::create("Fusion"))
        app.setStyle(fs);
    app.setStyleSheet(QString::fromUtf8(kGlobalQss));

    qRegisterMetaType<Joint::Telemetry>("Joint::Telemetry");
    qRegisterMetaType<Joint::TargetCommand>("Joint::TargetCommand");
    qRegisterMetaType<Joint::OperateMode>("Joint::OperateMode");
    qRegisterMetaType<AppConfig>("AppConfig");
    qRegisterMetaType<QList<Joint::Telemetry>>("QList<Joint::Telemetry>");
    qRegisterMetaType<QList<quint16>>("QList<quint16>");

    MainWindow w;
    w.show();
    return app.exec();
}
