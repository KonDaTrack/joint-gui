#include "ui/MainWindow.h"
#include <QLabel>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("关节模组监控台"));
    setCentralWidget(new QLabel(QStringLiteral("骨架窗口"), this));
    resize(960, 600);
}
