#include "ui/MonitorPanel.h"
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

QLabel* MonitorPanel::value(const char* objectName)
{
    QLabel* lab = new QLabel(QStringLiteral("--"), this);
    const bool big = objectName && !qstrcmp(objectName, "bigValue");
    if (big) {
        // 数值框固定宽度并右对齐：否则布局会把它拉满整行，
        // 右边留一大条空白，重心失衡。右对齐符合工业仪表读数习惯。
        // 宽度固定、高度可随行高伸展：三个读数槽均分左列剩余高度，撑满数据区。
        // 但设上限，避免窗口拉高时读数槽被拉成一大块空框（超出部分转为行间距）。
        lab->setFixedWidth(182);
        lab->setMinimumHeight(48);
        lab->setMaximumHeight(84);
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
    q = addRow(right, q, rightBox, tr("刷新率"), "lblRight", p.freq, true);

    // ---- 刷新率下面那块空地：负载（磁粉制动器）预设 ----
    // 负载走 RS485 → Modbus → 0-10V，与关节的 EtherCAT 是**两条独立链路**。
    // 这里设的是**预设值**：点「下发目标」时才真正施加（先写负载、确认成功再发运动），
    // 运动结束会自动清零。额定 50 N·m ↔ 10V。
    //
    // 注意负载是**全局**的（一路 RS485 驱动一个制动器，不随从站切换），
    // 而控件在每页里各有一份 —— 所以 setLoadState() 会把数值同步到所有页，
    // 编辑任一份也走同一个预设，多从站时不会出现"几个显示不同值的同一个东西"。
    p.loadSpin = new QDoubleSpinBox(rightBox);
    p.loadSpin->setRange(0.0, 200.0);   // 上限给宽：超额定由下位机硬拒绝并说明原因，不在这里夹
    p.loadSpin->setDecimals(1);
    p.loadSpin->setSingleStep(1.0);
    p.loadSpin->setSuffix(tr(" N·m"));
    p.loadSpin->setMinimumWidth(118);   // 再窄 "50.0 N·m" 会被挤到只剩省略号
    p.loadSpin->setToolTip(tr("磁粉制动器力矩负载（RS485 → Modbus → 0-10V）。\n"
                              "这里设的是预设值：点「下发目标」时先施加负载、确认成功后才发运动指令；\n"
                              "运动结束会自动清零。额定 50 N·m ↔ 10V。"));
    connect(p.loadSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                if (syncingLoad_) return;   // 程序化同步，不是用户改的
                emit loadPresetChanged(v);
            });

    p.loadRelease = new QPushButton(tr("松开"), rightBox);
    p.loadRelease->setToolTip(tr("立刻把负载清零，但**不让关节失能**。\n"
                                 "堵转时负载会一直保持、没有超时，用它撤载；\n"
                                 "失能/急停虽然也会撤载，但会把关节一起失能。"));
    connect(p.loadRelease, &QPushButton::clicked, this,
            [this] { emit releaseLoadRequested(); });

    QWidget* loadRow = new QWidget(rightBox);
    QHBoxLayout* loadLay = new QHBoxLayout(loadRow);
    loadLay->setContentsMargins(0, 0, 0, 0);
    loadLay->setSpacing(6);
    loadLay->addWidget(p.loadSpin, 1);
    loadLay->addWidget(p.loadRelease);
    q = addRow(right, q, rightBox, tr("负载预设"), "lblRight", loadRow, false);

    p.loadNote = value("valText");
    right->addWidget(p.loadNote, q, 0, 1, 2);   // 跨两列：状态文字比标题宽
    ++q;
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

void MonitorPanel::setLoadState(const QString& state, double presetNm, double appliedNm,
                                double volt, const QString& note)
{
    if (pages_.isEmpty())
        return;

    // 数值同步到所有页。要把信号挡住，否则 setValue 会反过来发 loadPresetChanged。
    syncingLoad_ = true;
    for (auto it = pages_.begin(); it != pages_.end(); ++it) {
        if (it->loadSpin && qAbs(it->loadSpin->value() - presetNm) > 0.05)
            it->loadSpin->setValue(presetNm);
    }
    syncingLoad_ = false;

    // 状态文字保持简短——这一列只有约 1/3 面板宽，长句子会把布局撑破。
    // 完整原因（比如 modbus_ao 的 stderr）放 tooltip。
    QString text;
    QString color = QStringLiteral("#94A3B8");   // 默认中性
    if (state == QLatin1String("applied")) {
        text = tr("已生效 %1 N·m").arg(appliedNm, 0, 'f', 1);
        color = QStringLiteral("#F59E0B");       // 琥珀：正在加载，要显眼
    } else if (state == QLatin1String("writing")) {
        text = tr("写入中…");
        color = QStringLiteral("#F59E0B");
    } else if (state == QLatin1String("failed")) {
        text = tr("写入失败");
        color = QStringLiteral("#F87171");
    } else if (state == QLatin1String("preset")) {
        text = tr("已预设，下发时施加");
    } else if (state == QLatin1String("unknown") || appliedNm < 0.0) {
        // 没能确认（-1）就说"未确认"，不假装 0、也不报红——
        // 开发机上没有 modbus_ao 是常态，那种红色会变成噪音
        text = tr("未确认");
    } else {
        text = tr("0 N·m");
    }

    for (auto it = pages_.begin(); it != pages_.end(); ++it) {
        if (!it->loadNote) continue;
        it->loadNote->setText(text);
        it->loadNote->setStyleSheet(QStringLiteral("color: %1;").arg(color));
        it->loadNote->setToolTip(note.isEmpty()
                                 ? tr("%1（%2 V）").arg(appliedNm, 0, 'f', 1).arg(volt, 0, 'f', 2)
                                 : note);
    }
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
