#include "app/Application.h"

int main(int argc, char* argv[])
{
    launcher::Application application(argc, argv);
    return application.run();
}
