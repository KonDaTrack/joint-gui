#include "ui/MonitorPanel.h"
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QVBoxLayout>

QLabel* MonitorPanel::value(const char* objectName)
{
    QLabel* lab = new QLabel(QStringLiteral("--"), this);
    const bool big = objectName && !qstrcmp(objectName, "bigValue");
    if (big) {
        // 数值框固定宽度并右对齐：否则 QFormLayout 会把它拉满整行，
        // 右边留一大条空白，重心失衡。右对齐符合工业仪表读数习惯。
        lab->setFixedWidth(150);
        lab->setFixedHeight(42);   // 固定高度，配合收紧的行距形成均匀节奏（随字号一起放大）
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

// 遥测行之间的 1px 分隔线（QFormLayout 里整行插入）
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
    QHBoxLayout* h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    h->addWidget(plate);
    QLabel* u = new QLabel(unit, box);
    u->setObjectName(QStringLiteral("unitText"));
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

MonitorPanel::Page MonitorPanel::makePage(quint16 slave)
{
    Page p;
    QWidget* w = new QWidget(tabs_);
    w->setObjectName(QStringLiteral("pageWidget"));   // 对应 QSS 限定选择器，透明底
    QFormLayout* form = new QFormLayout(w);
    form->setHorizontalSpacing(16);   // 键名与数值之间留出呼吸感
    form->setVerticalSpacing(6);      // 收紧了行距，避免读数框与下方遥测文本节奏断层
    // 行标题靠右贴住数值列：默认左对齐时，短标题与数值之间会留一段忽大忽小的空档
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // 识别出的型号：供核对本从站参数（额定力矩/减速比）是否配对，避免多关节错配
    p.model  = value("modelText"); form->addRow(tr("关节型号"), p.model);
    // 位置/速度/力矩为实时核心数据：内凹读数槽 + 等宽数字，单位独立成小字
    p.pos    = value("bigValue");  form->addRow(tr("位置"), withUnit(p.pos, tr("deg")));
    p.vel    = value("bigValue");  form->addRow(tr("速度"), withUnit(p.vel, tr("deg/s")));
    p.tor    = value("bigValue");  form->addRow(tr("力矩"), withUnit(p.tor, tr("N·m")));
    form->addRow(rowSep());
    // 下方遥测：每行一条 1px 分隔线，形成"数据条"的排版节奏
    p.temp   = value("valText");   form->addRow(tr("驱动器温度"), p.temp);
    form->addRow(rowSep());
    p.status = value("valText");   form->addRow(tr("状态字"), p.status);
    form->addRow(rowSep());
    p.state  = value("valText");   p.stateDot = dot();
    form->addRow(tr("驱动状态"), withDot(p.stateDot, p.state));
    form->addRow(rowSep());
    p.err    = value("valText");   form->addRow(tr("故障码"), p.err);
    form->addRow(rowSep());
    p.conn   = value("valText");   p.connDot = dot();
    form->addRow(tr("连接状态"), withDot(p.connDot, p.conn));
    form->addRow(rowSep());
    p.freq   = value("valText");   form->addRow(tr("刷新率"), p.freq);
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
