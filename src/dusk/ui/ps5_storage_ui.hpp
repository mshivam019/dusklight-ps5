#pragma once
#ifdef __PROSPERO__
#include <functional>
#include <string>
namespace dusk::ui {
void open_ps5_disc_storage(std::string path,std::function<void(std::string)> selected);
void open_ps5_save_folder_picker(std::function<void(std::string)> selected,bool canCopy);
}
#endif
