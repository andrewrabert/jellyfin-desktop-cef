#include "cef_app.h"
#include <iostream>

void App::OnContextInitialized() {
    std::cout << "CEF context initialized" << std::endl;
}
