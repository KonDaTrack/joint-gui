#include "ui/MonitorPanel.h"
#include <QDateTime>
#include <QHBoxLayout>
#include <QStyle>
#include <QVBoxLayout>

QLabel* MonitorPanel::value(const char* objectName)
{
    QLabel* lab = new QLabel(QStringLiteral("--"), this);
    const bool big = objectName && !qstrcmp(objectName, "bigValue");
    if (big) {
        // 数值框固定宽度并右对齐：否则布局会把它拉满整行，
        // 右边留一大条空白，重心失衡。右对齐符合工业仪表读数习惯。
        // 宽度固定、高度可随行高伸展：让三个读数槽均分左列剩余高度，撑满数据区
        lab->setFixedWidth(210);
        lab->setMinimumHeight(56);
        lab->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        lab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    } else {
        lab->setMinimumHeight(24);
    }
    if (objectName && *objectName) {
        lab->setObjectName(QString::fromLatin1(objectName));
        lab->style()->unpolish(lab);
        lab->style()->polish(lab);
    }
    return lab;
}

// 8px 状态指示灯：形状/底色由 QSS(#statusDot) 决定，颜色由 updatePage 按状态改写
QLabel* MonitorPanel::dot()
{
    QLabel* d = new QLabel(this);
    d->setObjectName(QStringLiteral("statusDot"));
    d->style()->unpolish(d);
    d->style()->polish(d);
    return d;
}

// 指示灯 + 文字：状态行前面加一个圆点，比纯文字更直观
QWidget* MonitorPanel::withDot(QLabel* d, QLabel* text)
{
    QWidget* box = new QWidget(this);
    box->setObjectName(QStringLiteral("dotRow"));
    QHBoxLayout* h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(7);
    h->addWidget(d);
    h->addWidget(text);
    h->addStretch();
    return box;
}

// 遥测行之间的 1px 分隔线（网格里跨两列插入）
QFrame* MonitorPanel::rowSep()
{
    QFrame* f = new QFrame(this);
    f->setObjectName(QStringLiteral("rowSep"));
    f->setFixedHeight(1);
    return f;
}

// 数值框右侧跟一个浅灰单位小字（如 [ 0.00 ] deg），比把单位塞进行标题更接近仪表观感
QWidget* MonitorPanel::withUnit(QLabel* plate, const QString& unit)
{
    QWidget* box = new QWidget(this);
    box->setObjectName(QStringLiteral("unitRow"));
    box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);   // 随行高伸展
    QHBoxLayout* h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    h->addWidget(plate);
    QLabel* u = new QLabel(unit, box);
    u->setObjectName(QStringLiteral("unitText"));
    u->setAlignment(Qt::AlignVCenter);   // 读数槽变高时单位保持垂直居中
    h->addWidget(u);
    h->addStretch();
    return box;
}

MonitorPanel::MonitorPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PanelCard"));
    setAttribute(Qt::WA_StyledBackground, true);

    tabs_ = new QTabWidget(this);
    // 点标签页 = 切换当前从站（查看 + 控制 + 曲线三处统一），对外发信号
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int idx) {
        if (syncing_ || idx < 0 || idx >= order_.size()) return;
        emit activeSlaveChanged(order_.at(idx));
    });
    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->addWidget(tabs_);
}

// 往网格加一行「标题 | 值」。用 QGridLayout 而非 QFormLayout，
// 是因为 QFormLayout 的标题是内部创建的、无法单独打 objectName，
// 而左右两列需要不同的标题字号。
int MonitorPanel::addRow(QGridLayout* g, int row, QWidget* parent, const QString& text,
                         const char* labelObject, QWidget* v, bool withSep)
{
    QLabel* l = new QLabel(text, parent);
    l->setObjectName(QString::fromLatin1(labelObject));
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    g->addWidget(l, row, 0);
    g->addWidget(v, row, 1);
    g->setColumnStretch(1, 1);
    if (withSep) g->addWidget(rowSep(), row + 1, 0, 1, 2);
    return row + (withSep ? 2 : 1);
}

MonitorPanel::Page MonitorPanel::makePage(quint16 slave)
{
    Page p;
    QWidget* w = new QWidget(tabs_);
    w->setObjectName(QStringLiteral("pageWidget"));   // 对应 QSS 限定选择器，透明底
    // 双列布局：单列时右半边整片空白。左列只放核心读数（字号/行距更大，
    // 是操作时最常看的数据），右列放状态类字段。
    QHBoxLayout* cols = new QHBoxLayout(w);
    cols->setContentsMargins(0, 0, 0, 0);
    cols->setSpacing(24);

    QWidget* leftBox = new QWidget(w);
    leftBox->setObjectName(QStringLiteral("colLeft"));
    QGridLayout* left = new QGridLayout(leftBox);
    left->setContentsMargins(0, 0, 0, 0);
    left->setHorizontalSpacing(22);
    left->setVerticalSpacing(14);    // 左列行距拉大

    QWidget* rightBox = new QWidget(w);
    rightBox->setObjectName(QStringLiteral("colRight"));
    QGridLayout* right = new QGridLayout(rightBox);
    right->setContentsMargins(0, 0, 0, 0);
    right->setHorizontalSpacing(16);
    right->setVerticalSpacing(6);

    // ---- 左列：核心读数 ----
    int r = 0;
    // 识别出的型号：供核对本从站参数（额定力矩/减速比）是否配对，避免多关节错配
    p.model = value("modelText");
    r = addRow(left, r, leftBox, tr("关节型号"), "lblLeft", p.model, false);
    // 位置/速度/力矩：内凹读数槽 + 等宽数字，单位独立成小字
    p.pos = value("bigValue");
    r = addRow(left, r, leftBox, tr("位置"), "lblLeft", withUnit(p.pos, tr("deg")), false);
    p.vel = value("bigValue");
    r = addRow(left, r, leftBox, tr("速度"), "lblLeft", withUnit(p.vel, tr("deg/s")), false);
    p.tor = value("bigValue");
    r = addRow(left, r, leftBox, tr("力矩"), "lblLeft", withUnit(p.tor, tr("N·m")), false);
    // 型号行保持紧凑，三个读数槽均分剩余高度 → 撑满数据区而非只在底部留白
    left->setRowStretch(0, 0);
    for (int i = 1; i < r; ++i) left->setRowStretch(i, 1);

    // ---- 右列：状态类字段，每行一条 1px 分隔线形成"数据条"节奏 ----
    int q = 0;
    p.status = value("valText");
    q = addRow(right, q, rightBox, tr("状态字"), "lblRight", p.status, true);
    p.state = value("valText");  p.stateDot = dot();
    q = addRow(right, q, rightBox, tr("驱动状态"), "lblRight",
               withDot(p.stateDot, p.state), true);
    p.err = value("valText");
    q = addRow(right, q, rightBox, tr("故障码"), "lblRight", p.err, true);
    p.conn = value("valText");   p.connDot = dot();
    q = addRow(right, q, rightBox, tr("连接状态"), "lblRight",
               withDot(p.connDot, p.conn), true);
    p.temp = value("valText");
    q = addRow(right, q, rightBox, tr("驱动器温度"), "lblRight", p.temp, true);
    p.freq = value("valText");
    q = addRow(right, q, rightBox, tr("刷新率"), "lblRight", p.freq, false);
    right->setRowStretch(q, 1);

    // 两列之间的细分割线，强化"双栏"结构
    QFrame* divider = new QFrame(w);
    divider->setObjectName(QStringLiteral("colSep"));
    divider->setFrameShape(QFrame::VLine);
    divider->setFixedWidth(1);

    cols->addWidget(leftBox, 3);
    cols->addWidget(divider);
    cols->addWidget(rightBox, 2);
    cols->addStretch();

    p.page = w;
    Q_UNUSED(slave);
    return p;
}

void MonitorPanel::setSlaves(const QList<quint16>& slaves)
{
    // 重建标签页期间 addTab/removeTab 会触发 currentChanged，
    // 必须抑制，否则会发出假的 activeSlaveChanged 把 worker 的控制目标带偏
    syncing_ = true;
    while (tabs_->count() > 0) {
        QWidget* w = tabs_->widget(0);
        tabs_->removeTab(0);
        delete w;   // 页内 QLabel 随页释放
    }
    pages_.clear();
    order_ = slaves;
    shorts_.clear();   // 型号随重连变化，等 setSlaveModels 再填
    for (quint16 s : slaves) {
        Page p = makePage(s);
        pages_.insert(s, p);
        tabs_->addTab(p.page, QStringLiteral("从站 %1").arg(s));
    }
    if (slaves.isEmpty()) {
        tabs_->addTab(new QLabel(QStringLiteral("未连接"), tabs_), QStringLiteral("--"));
    }
    syncing_ = false;
}

// 下标 i ↔ 从站 i+1（与 slaveList() 同序）
void MonitorPanel::setSlaveModels(const QStringList& shortNames, const QStringList& modelInfos)
{
    shorts_ = shortNames;
    for (quint16 s : order_) {
        const int idx = s - 1;
        if (idx < 0 || idx >= modelInfos.size()) continue;
        const auto it = pages_.find(s);
        if (it == pages_.end() || !it->model) continue;
        const QString t = modelInfos.at(idx);
        it->model->setText(t.isEmpty() ? QStringLiteral("--") : t);
        // 标签页标题带上型号短名（如「从站2 · 70mm」），不点开也能分清哪个轴
        const QString sn = (idx < shorts_.size()) ? shorts_.at(idx) : QString();
        tabs_->setTabText(idx, sn.isEmpty() ? QStringLiteral("从站 %1").arg(s)
                                            : QStringLiteral("从站%1 · %2").arg(s).arg(sn));
    }
}

void MonitorPanel::setActiveSlave(quint16 address)
{
    const int idx = order_.indexOf(address);
    if (idx < 0) return;
    syncing_ = true;                 // 抑制信号：这是程序化同步，不是用户切换
    tabs_->setCurrentIndex(idx);
    syncing_ = false;
}

void MonitorPanel::onTelemetry(const QList<Joint::Telemetry>& list)
{
    for (const Joint::Telemetry& t : list) {
        auto it = pages_.find(t.slave);
        if (it != pages_.end()) updatePage(*it, t);
    }
}

void MonitorPanel::updatePage(Page& p, const Joint::Telemetry& t)
{
    p.pos->setText(QString::number(t.positionDeg, 'f', 2));
    p.vel->setText(QString::number(t.velocityDps, 'f', 2));
    p.tor->setText(QString::number(t.torqueNm, 'f', 3));
    p.temp->setText(t.temperatureC > 0 ? QString::number(t.temperatureC, 'f', 1)
                                       : QStringLiteral("N/A"));
    p.status->setText(QStringLiteral("0x%1").arg(t.statusWord, 4, 16, QLatin1Char('0')));

    const char* stateStr = "未知";
    switch (t.driveState) {
    case Joint::DriveState::NotReadyToSwitchOn: stateStr = "未就绪"; break;
    case Joint::DriveState::SwitchOnDisabled:   stateStr = "禁止合闸"; break;
    case Joint::DriveState::ReadyToSwitchOn:    stateStr = "待合闸"; break;
    case Joint::DriveState::SwitchedOn:         stateStr = "已合闸"; break;
    case Joint::DriveState::OperationEnabled:   stateStr = "运行使能"; break;
    case Joint::DriveState::QuickStopActive:    stateStr = "快速停机"; break;
    case Joint::DriveState::FaultReactionActive: stateStr = "故障反应"; break;
    case Joint::DriveState::Fault:              stateStr = "故障"; break;
    case Joint::DriveState::Unknown: break;
    }
    p.state->setText(QString::fromUtf8(stateStr));
    // 驱动状态按工业状态色着色：运行使能→绿 / 故障→红 / 故障反应·快速停机→黄
    const char* stateColor = "#94A3B8";
    switch (t.driveState) {
    case Joint::DriveState::OperationEnabled:  stateColor = "#34D399"; break;
    case Joint::DriveState::Fault:             stateColor = "#F87171"; break;
    case Joint::DriveState::FaultReactionActive:
    case Joint::DriveState::QuickStopActive:   stateColor = "#F59E0B"; break;
    default: break;
    }
    p.state->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(stateColor));
    // 指示灯：只改颜色，几何由 QSS #statusDot 统一（否则正常态加光晕会让圆点变大）
    // 正常态给深绿描边模拟自发光，其余状态描边与底色同色（等于无光晕）
    if (p.stateDot) {
        const bool okState = (t.driveState == Joint::DriveState::OperationEnabled);
        p.stateDot->setStyleSheet(
            QStringLiteral("background-color: %1; border-color: %2;")
                .arg(stateColor, okState ? QStringLiteral("#14532D") : QString(stateColor)));
    }

    p.err->setText(t.errorCode ? QStringLiteral("0x%1").arg(t.errorCode, 4, 16, QLatin1Char('0'))
                               : QStringLiteral("无"));
    // 断开时中性色，避免"离线"旁显示绿色"无"造成误导
    p.err->setStyleSheet(t.connected
                         ? (t.errorCode ? QStringLiteral("color: #F87171; font-weight: bold;")
                                        : QStringLiteral("color: #34D399;"))
                         : QStringLiteral("color: #94A3B8;"));

    p.conn->setText(t.connected ? QStringLiteral("在线") : QStringLiteral("离线"));
    p.conn->setStyleSheet(t.connected ? QStringLiteral("color: #34D399; font-weight: bold;")
                                      : QStringLiteral("color: #F87171; font-weight: bold;"));
    if (p.connDot) {
        p.connDot->setStyleSheet(
            t.connected
                ? QStringLiteral("background-color: #34D399; border-color: #14532D;")
                : QStringLiteral("background-color: #F87171; border-color: #F87171;"));
    }

    ++p.samples;
    if (p.lastFreqMs == 0) p.lastFreqMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (p.samples >= 20) {
        p.freqHz = 1000.0 * p.samples / (now - p.lastFreqMs);
        p.samples = 0;
        p.lastFreqMs = now;
        p.freq->setText(QStringLiteral("%1 Hz").arg(p.freqHz, 0, 'f', 1));
    }
}
