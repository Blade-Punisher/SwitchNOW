#pragma once
#include <memory>
#include <string>
#include <vector>
namespace brls {
struct Action {
    std::string text;
    std::string getHintText() const{return text;}
    void setHintText(const std::string& value){text=value;}
};
class Box;
class View {
public:
    Box* parent=nullptr;
    std::vector<std::shared_ptr<Action>> actions;
    Box* getParent(){return parent;}
    inline static unsigned reads=0;
    const auto& getActions() const{++reads;return actions;}
};
class Box:public View {};
struct Application {
    inline static View* focus=nullptr;
    static View* getCurrentFocus(){return focus;}
};
}
