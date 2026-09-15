#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>
#include "device/JointTypes.h"
#include "ui/MainWindow.h"

// 全局深色工业科技风主题（Dark Industrial Tech）。
// 仅使用原生 Qt 5 QSS，零第三方依赖。
// 色彩系统（Slate 系，柔和不刺眼）：
//   全局背景  #16181D      功能卡片  #1E2128（8px 圆角，1px #2B303C 边框）
//   控件内底  #13151A      主文字    #E2E8F0    次文字  #94A3B8
//   科技强调  #38BDF8 / 按下 #0284C7
//   实时数据  #34D399（薄荷绿，等宽 + 内嵌底框）
//   工业状态  绿 #34D399 / 黄 #F59E0B / 红 #F87171
//   按钮角色  primaryButton(蓝) / dangerButton(实心红·急停) /
//             warningButton(橙描边·故障复位) / dangerActionButton(红描边·停止运动)
static const char kGlobalQss[] = R"(
/* ============ 全局 ============ */
QWidget {
    /* Linux 优先的中文字体栈：Ubuntu 上没有 PingFang/雅黑，
       落到系统默认字体正是界面显"糙"的原因之一 */
    font-family: "Inter", "Segoe UI", "Noto Sans CJK SC", "Source Han Sans SC",
                 "WenQuanYi Micro Hei", "Microsoft YaHei", sans-serif;
    background-color: #16181D;
    color: #E2E8F0;
    font-size: 14px;
    selection-background-color: #0284C7;
    selection-color: #FFFFFF;
}
QMainWindow { background-color: #16181D; }
QWidget#centralRoot { background-color: #16181D; }
/* 标签页内部容器透明，露出 QTabWidget::pane 的统一卡片底色。
   用 #pageWidget 限定，避免未来标签页内的交互控件（下拉/输入框）被透明覆盖丢背景。 */
QTabWidget QWidget#pageWidget { background: transparent; }

QLabel {
    background: transparent;
    color: #E2E8F0;
}

/* 监控页：QFormLayout 的行标题调暗，值列提亮 —— 形成清晰的"参数名 | 数值"两列层级。
   行标题由 QFormLayout 内部创建，只能靠父级限定选择器命中 */
QTabWidget QWidget#pageWidget QLabel { color: #8D949E; }
QLabel#valText { color: #E2E8F0; }
QLabel#unitText { color: #646C7A; font-size: 13px; }
QWidget#unitRow { background: transparent; }

/* ============ 功能卡片 ============ */
/* 注意：监控/控制/曲线面板都是 QWidget 子类（不是 QFrame），
   类型选择器必须写 QWidget#PanelCard，写 QFrame#... 会静默不匹配 */
QWidget#PanelCard {
    background-color: #1E2128;
    border: 1px solid #2B303C;
    border-radius: 8px;
}

/* ============ 按钮 ============ */
QPushButton {
    background-color: #262A34;
    border: 1px solid #373D4B;
    border-radius: 5px;
    /* 左右 padding 由 16px 收到 10px：5 个按钮并排时自然宽度会超出右栏可用宽度 */
    padding: 6px 10px;
    color: #E2E8F0;
    min-height: 24px;
}
QPushButton:hover { background-color: #2B303C; border-color: #0284C7; }
QPushButton:pressed { background-color: #1A1D24; }
QPushButton:focus { border-color: #0284C7; }
QPushButton:disabled { color: #646C7A; background-color: #1A1D24; border-color: #262A34; }

QPushButton#primaryButton {
    background-color: #0284C7;
    border: 1px solid #0284C7;
    color: #FFFFFF;
    font-weight: bold;
}
/* 悬停/按下用更深的蓝：白字压在浅蓝(#38BDF8)上对比度不足 */
QPushButton#primaryButton:hover { background-color: #0369A1; border-color: #38BDF8; }
QPushButton#primaryButton:pressed { background-color: #075985; }
QPushButton#primaryButton:disabled { background-color: #1A1D24; color: #646C7A; border-color: #262A34; }

/* 故障复位：橙色描边警示（可恢复操作） */
QPushButton#warningButton {
    background-color: #27231B;
    border: 1px solid #F59E0B;
    color: #FBBF24;
    font-weight: bold;
}
QPushButton#warningButton:hover { background-color: #382E1E; border-color: #FBBF24; }
QPushButton#warningButton:pressed { background-color: #1F1912; }

/* 停止运动：暗红描边（危险动作，与故障复位区分） */
QPushButton#dangerActionButton {
    background-color: #2B1D20;
    border: 1px solid #F87171;
    color: #FCA5A5;
    font-weight: bold;
}
QPushButton#dangerActionButton:hover { background-color: #382327; border-color: #FCA5A5; }
QPushButton#dangerActionButton:pressed { background-color: #23171A; }

QPushButton#dangerButton {
    background-color: #DC2626;
    border: 1px solid #DC2626;
    color: #FFFFFF;
    border-radius: 6px;
    font-size: 20px;
    font-weight: bold;
    min-height: 52px;
}
QPushButton#dangerButton:hover { background-color: #EF4444; border-color: #FF8A8A; }
QPushButton#dangerButton:pressed { background-color: #B91C1C; }

QMessageBox QPushButton { min-width: 80px; }

/* ============ 输入控件 ============ */
QLineEdit {
    background-color: #16181D;
    border: 1px solid #2B303C;
    border-radius: 4px;
    padding: 5px 8px;
    color: #E2E8F0;
    min-height: 22px;
}
QLineEdit:focus { border-color: #0284C7; }
QLineEdit:hover { border-color: #4B5366; }
QLineEdit:disabled { color: #646C7A; background-color: #1A1D24; }

QComboBox {
    background-color: #16181D;
    border: 1px solid #2B303C;
    border-radius: 4px;
    padding: 5px 28px 5px 10px;   /* 右侧留足箭头安全边距，避免箭头压到文字 */
    color: #E2E8F0;
    min-height: 22px;
}
QComboBox:focus { border-color: #0284C7; }
QComboBox:hover { border-color: #4B5366; }
QComboBox:disabled { color: #646C7A; background-color: #1A1D24; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 24px;
    border: none;
    background: transparent;
}
/* 纯 CSS 边框三角（免素材）。关键：必须显式给 width/height:0，
   否则 subcontrol 会按默认尺寸渲染成一个实心方块（之前就是这个症状）。 */
QComboBox::down-arrow {
    width: 0;
    height: 0;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 6px solid #94A3B8;
    margin-right: 8px;
}
QComboBox::down-arrow:hover { border-top-color: #38BDF8; }
QComboBox QAbstractItemView {
    background-color: #1E2128;
    color: #E2E8F0;
    border: 1px solid #2B303C;
    selection-background-color: #0284C7;
    selection-color: #FFFFFF;
    outline: 0;
}

QCheckBox { color: #E2E8F0; spacing: 8px; }
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #373D4B;
    border-radius: 3px;
    background-color: #16181D;
}
QCheckBox::indicator:hover { border-color: #0284C7; }
QCheckBox::indicator:checked { background-color: #0284C7; border-color: #0284C7; }
QCheckBox::indicator:disabled { background-color: #1A1D24; border-color: #262A34; }

/* ============ 标签页 ============ */
QTabWidget::pane {
    border: 1px solid #2B303C;
    border-radius: 4px;
    top: -1px;
    background-color: #1E2128;
}
QTabBar::tab {
    background-color: #1A1D24;
    color: #94A3B8;
    border: 1px solid #2B303C;
    border-bottom: none;
    padding: 9px 22px;
    margin-right: 3px;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    font-size: 14px;
}
QTabBar::tab:hover:!selected { background-color: #242831; color: #CBD5E1; }
/* 选中页：顶部高亮指示条 + 与面板同色，视觉上"融进"下方内容区 */
QTabBar::tab:selected {
    background-color: #1E2128;
    color: #38BDF8;
    border-top: 2px solid #38BDF8;
    border-left: 1px solid #2B303C;
    border-right: 1px solid #2B303C;
    border-bottom: 1px solid #1E2128;
    font-weight: bold;
}

/* ============ 滚动区 / 滚动条 ============ */
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical {
    background: #16181D;
    width: 12px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #373D4B;
    border-radius: 6px;
    min-height: 30px;
    margin: 2px;
}
QScrollBar::handle:vertical:hover { background: #4B5366; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; background: none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
QScrollBar:horizontal {
    background: #16181D;
    height: 12px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #373D4B;
    border-radius: 6px;
    min-width: 30px;
    margin: 2px;
}
QScrollBar::handle:horizontal:hover { background: #4B5366; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; background: none; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }

/* ============ 分隔条 ============ */
QSplitter::handle { background-color: #16181D; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }
QSplitter::handle:hover { background-color: #0284C7; }

/* ============ 状态栏 ============ */
QStatusBar {
    background-color: #1A1D24;
    border-top: 1px solid #2B303C;
    color: #E2E8F0;
}
QStatusBar::item { border: none; }

/* ============ 对话框 / 提示 ============ */
QDialog { background-color: #16181D; }
QMessageBox { background-color: #16181D; }
QMessageBox QLabel { color: #E2E8F0; }
QDialog QLabel#dialogTitle { font-size: 16px; font-weight: bold; color: #38BDF8; }
QToolTip {
    background-color: #1E2128;
    color: #E2E8F0;
    border: 1px solid #2B303C;
    padding: 4px 8px;
}

/* ============ 实时数据大字号 ============ */
/* 三个关键遥测（位置/速度/力矩）：等宽数字 + 内嵌深色底框，像工业仪表读数。
   等宽保证数值跳动时宽度不抖，底框让数值从背景里"立"起来 */
QLabel#bigValue {
    background-color: #13151A;
    border: 1px solid #282C37;
    border-radius: 5px;
    padding: 3px 10px;
    color: #34D399;
    font-size: 19px;
    font-weight: bold;
    font-family: "JetBrains Mono", "DejaVu Sans Mono", "Consolas", monospace;
}
QLabel#sectionTitle {
    color: #38BDF8;
    font-weight: bold;
    font-size: 15px;
    margin-top: 4px;
}
)";

// Fusion 自绘的部分（下拉箭头、菜单、勾选框等）取的是调色板颜色而非 QSS。
// 不设深色调色板时，这些部位会用默认浅色调色板的深色前景 → 深底上几乎看不见。
static void applyDarkPalette(QApplication& app)
{
    QPalette p = app.palette();
    p.setColor(QPalette::Window,          QColor(0x16, 0x18, 0x1D));
    p.setColor(QPalette::WindowText,      QColor(0xE2, 0xE8, 0xF0));
    p.setColor(QPalette::Base,            QColor(0x13, 0x15, 0x1A));
    p.setColor(QPalette::AlternateBase,   QColor(0x1E, 0x21, 0x28));
    p.setColor(QPalette::Text,            QColor(0xE2, 0xE8, 0xF0));
    p.setColor(QPalette::Button,          QColor(0x26, 0x2A, 0x34));
    p.setColor(QPalette::ButtonText,      QColor(0xE2, 0xE8, 0xF0));
    p.setColor(QPalette::Highlight,       QColor(0x02, 0x84, 0xC7));
    p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    p.setColor(QPalette::ToolTipBase,     QColor(0x1E, 0x21, 0x28));
    p.setColor(QPalette::ToolTipText,     QColor(0xE2, 0xE8, 0xF0));
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x64, 0x6C, 0x7A));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x64, 0x6C, 0x7A));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x64, 0x6C, 0x7A));
    app.setPalette(p);
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Fusion 内建样式 + 深色调色板 + QSS，保证深色主题跨控件渲染一致
    if (QStyle* fs = QStyleFactory::create("Fusion"))
        app.setStyle(fs);
    applyDarkPalette(app);
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
