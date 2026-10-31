#include "card_viewport_policy.hpp"
#include <cassert>
#include <limits>
#include <initializer_list>
int main() {
    using namespace opennow::ui;
    CardRect viewport{0,0,1280,600};
    assert(CardInViewport({10,10,224,178},viewport,20));
    assert(!CardInViewport({10,650,224,178},viewport,20));
    assert(CardInViewport({10,650,224,178},viewport,380)); // prefetch, not draw
    assert(!CardInViewport({10,1100,224,178},viewport,380)); // release texture
    assert(CardInViewport({10,-190,224,178},viewport,20)); // shadow overlaps edge
    assert(!CardInViewport({10,-210,224,178},viewport,20));
    assert(!CardInViewport({0,0,0,178},viewport,20));
    assert(!CardInViewport({0,std::numeric_limits<float>::quiet_NaN(),224,178},viewport,20));
    // Growing from60 cards to10000 cannot grow per-frame drawing or live GPU
    // thumbnail ownership beyond the viewport plus its bounded warm margin.
    for(int offset: {0,190,950,190000}) {
        unsigned draw=0,images=0;
        for(int row=0;row<2000;++row) {
            CardRect card{0,float(row*190-offset),224,178};
            if(CardInViewport(card,viewport,20))draw+=5;
            if(CardInViewport(card,viewport,380))images+=5;
        }
        assert(draw<=25 && images<=45);
    }
}
