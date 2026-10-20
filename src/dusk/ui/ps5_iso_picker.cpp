#include "ps5_iso_picker.hpp"
#ifdef __PROSPERO__
#include "list.hpp"
#include "window.hpp"
#include "dusk/settings.h"
#include <filesystem>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <stdexcept>
namespace dusk::ui {
namespace {
namespace fs=std::filesystem;
bool opened=false;
using Selected=std::function<void(std::string)>;
struct Row {fs::path path; bool directory; std::string label;};
class IsoBrowser final:public WindowSmall {
public:
 explicit IsoBrowser(Selected selected):WindowSmall("modal"),selected(std::move(selected)) {
  mRoot->SetProperty("width","80vw");mRoot->SetProperty("max-width","1000dp");
  append_text(append(append(mDialog,"modal-header"),"modal-title"),"Select Disc Image");
  auto* body=append(mDialog,"modal-body");
  append_text(body,"Open a folder and choose your ISO.");
  pathLabel=append(body,"file-path");pathLabel->SetProperty("word-break","break-all");
  status=append(body,"div");bar=append(body,"progress");bar->SetClass("info",true);
  list=std::make_unique<List>(body,List::Props{.items={},.onPressed=[this](std::uint64_t key){choose(key);}});
  list->root()->SetProperty("height","34vh");list->root()->SetProperty("flex","0 0 auto");
  auto* actions=append(mDialog,"modal-actions");
  places=button(actions,"Locations",[this]{if(!busy)locations();});
  up=button(actions,"Back",[this]{if(!busy)back();});
  cancel=button(actions,"Cancel",[this]{if(busy)stop=true;else close();});
  locations();
 }
 ~IsoBrowser() override {stop=true;if(worker.joinable())worker.join();opened=false;}
 bool focus() override {return busy?cancel->focus():list->focus()||up->focus()||cancel->focus();}
 void update() override {
  WindowSmall::update();list->update();if(!busy)return;
  if(queued && ++frames>=2){queued=false;const auto path=folder;
   try{worker=std::thread([this,path]{
    try{
     if(!path.is_absolute()||fs::is_symlink(path))throw std::runtime_error("Cannot open this folder.");
     for(const auto& entry:fs::directory_iterator(path)){
      if(stop)break;++checked;std::error_code ec;
      if(entry.is_symlink(ec)||ec)continue;
      bool dir=entry.is_directory(ec);if(ec)continue;
      std::string ext=entry.path().extension().string();
      std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
      if(!dir && !(ext==".iso" || ext==".gcm"))continue;
      if(!dir && !entry.is_regular_file(ec))continue;
      rows.push_back({entry.path(),dir,entry.path().filename().string()+(dir?"/":"")});
     }
     std::sort(rows.begin(),rows.end(),[](const Row&a,const Row&b){return a.directory!=b.directory?a.directory:a.path.filename()<b.path.filename();});
    }catch(const std::exception& e){error=e.what();}
    done.store(true,std::memory_order_release);
   });}catch(const std::exception& e){error=e.what();done=true;}
  }
  if(done.load(std::memory_order_acquire)){
   if(worker.joinable())worker.join();busy=false;if(stop){close();return;}
   show();set_text_content(status,error);focus();return;
  }
  bar->SetAttribute("value",0.15f+0.7f*float(++pulse%120)/120.f);
  set_text_content(status,std::string("Reading folder... ")+std::to_string(checked.load()));
 }
protected:
 bool handle_nav_command(Rml::Event& e,NavCommand command) override {
  if(command==NavCommand::Cancel||command==NavCommand::Menu){if(busy)stop=true;else back();e.StopPropagation();return true;}
  std::vector<Button*> buttons{places.get(),up.get(),cancel.get()};
  auto current=std::find_if(buttons.begin(),buttons.end(),[&](Button*b){return b->contains(e.GetTargetElement());});
  bool handled=false;
  if(command==NavCommand::Down&&list->contains(e.GetTargetElement()))handled=places->focus();
  else if(command==NavCommand::Up&&current!=buttons.end()&&!busy)handled=list->focus_from(command);
  else if(current!=buttons.end()&&(command==NavCommand::Left||command==NavCommand::Right)){
   int step=command==NavCommand::Left?-1:1;
   for(int i=int(current-buttons.begin())+step;i>=0&&i<int(buttons.size());i+=step)if(buttons[i]->focus()){handled=true;break;}
  }
  if(handled)e.StopPropagation();return handled;
 }
private:
 std::unique_ptr<Button> button(Rml::Element*p,const char*text,ButtonCallback callback){
  auto b=std::make_unique<Button>(p,text);b->root()->SetClass("modal-btn",true);b->on_pressed(std::move(callback));return b;
 }
 void close(){opened=false;pop();}
 void show(){
  std::vector<List::Item> items;for(std::size_t i=0;i<rows.size();++i)items.push_back({i,rows[i].label});
  list->set_items(std::move(items));list->update();list->root()->SetProperty("display","flex");
  bar->SetProperty("display","none");places->set_disabled(false);up->set_disabled(folder.empty());
  set_text_content(pathLabel,folder.empty()?"Locations":folder.string());set_text_content(status,"");
 }
 void locations(){
  folder.clear();rows.clear();auto add=[this](const char*label,const fs::path&p){if(!p.empty())rows.push_back({p,true,std::string(label)+"  ("+p.string()+")"});};
  add("Last used folder",fs::path(getSettings().backend.isoPath.getValue()).parent_path());
  add("USB 0","/mnt/usb0");add("USB 1","/mnt/usb1");add("Data storage","/data");
  const char* app=std::getenv("DUSK_PS5_APP_ROOT");add("Application folder",app?app:"/app0");add("File system","/");show();
 }
 void back(){if(folder.empty()){close();return;}if(folder==folder.root_path())locations();else begin(folder.parent_path());}
 void choose(std::uint64_t key){if(busy||key>=rows.size())return;const auto row=rows[key];if(row.directory){begin(row.path);return;}auto callback=selected;close();callback(row.path.string());}
 void begin(const fs::path&path){
  folder=path.lexically_normal();rows.clear();error.clear();busy=true;queued=true;frames=pulse=0;checked=0;stop=false;done=false;
  list->root()->SetProperty("display","none");bar->SetProperty("display","block");places->set_disabled(true);up->set_disabled(true);
  set_text_content(pathLabel,folder.string());set_text_content(status,"Reading folder...");cancel->focus();
 }
 Selected selected;fs::path folder;std::vector<Row> rows;std::string error;
 std::thread worker;std::atomic_bool stop{false},done{false};std::atomic_uint64_t checked{0};
 bool busy=false,queued=false;unsigned frames=0,pulse=0;
 Rml::Element *pathLabel,*status,*bar;std::unique_ptr<List> list;std::unique_ptr<Button> places,up,cancel;
};
}
void open_ps5_iso_picker(std::function<void(std::string)> selected){if(opened)return;if(auto*host=top_document()){auto window=std::make_unique<IsoBrowser>(std::move(selected));opened=true;host->push(std::move(window));}}
}
#endif
