// actl_sim: giao diện đơn giản cho chương trình giả lập TRB (bản dòng lệnh là actl_sim_cli).
#include "sim_window.h"
#include "../ui/theme.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("actl_sim");
    ui::theme::apply(app);
    sim::SimWindow w;
    w.show();
    return app.exec();
}
