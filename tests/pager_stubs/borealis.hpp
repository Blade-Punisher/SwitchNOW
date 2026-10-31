#pragma once
#include <functional>
#include <string>
#include <vector>
namespace brls {
enum class Axis{ROW,COLUMN}; enum class Visibility{VISIBLE,GONE};
inline int BUTTONSTYLE_PRIMARY=1,BUTTONSTYLE_BORDERED=2;
struct View {
 virtual ~View()=default;
 Visibility visibility=Visibility::VISIBLE;
 void setMarginTop(float){} void setMarginBottom(float){} void setMarginRight(float){}
 float width=0,padding=0,shrink=1,grow=0,height=0;
 void setWidth(float value){width=value;} void setCornerRadius(float){}
 void setWidthPercentage(float){} void setHeight(float value){height=value;}
 void setShrink(float value){shrink=value;} void setGrow(float value){grow=value;}
 void setPadding(float,float right,float,float){padding=right;}
 virtual View* getView(std::string){return nullptr;}
 void setVisibility(Visibility v){visibility=v;}
};
struct Box: View {
 std::vector<View*> children;
 explicit Box(Axis){} ~Box() override {for(auto* child:children)delete child;}
 void addView(View* view){children.push_back(view);}
};
struct Label: View {bool single_line=false;std::string text;void setText(std::string s){text=std::move(s);} void setFontSize(float){} void setSingleLine(bool value){single_line=value;} };
struct Button: View {
 Label label; float font_size=0; void setFontSize(float value){font_size=value;}
 View* getView(std::string) override{return &label;}
 std::string text; int* style=nullptr; std::function<bool(View*)> click;
 void setStyle(int* value){style=value;} void setText(std::string s){text=std::move(s);}
 void registerClickAction(std::function<bool(View*)> f){click=std::move(f);}
};
struct Ime {
 std::function<void(long)> result;
 bool openForNumber(std::function<void(long)> f,std::string,std::string,int,std::string){result=std::move(f);return true;}
};
struct Application {
 inline static View* focus=nullptr;
 static View* getCurrentFocus(){return focus;} static void giveFocus(View* view){focus=view;}
 inline static Ime ime; inline static std::string notification;
 static Ime* getImeManager(){return &ime;} static void notify(std::string s){notification=std::move(s);}
};
}
