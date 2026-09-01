// Every test drives real widgets, so a QApplication has to exist before any of
// them runs. GoogleTest's own main() does not create one, hence this.

#include <QApplication>
#include <gtest/gtest.h>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
