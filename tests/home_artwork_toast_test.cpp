#include "home_artwork_toast_policy.hpp"
#include <cassert>
int main(){opennow::shortcut::ArtworkToastTimer t;assert(!t.Visible(0));t.Show(100);assert(t.Visible(100));assert(t.Visible(10099));assert(!t.Visible(10100));assert(!t.Visible(99));t.Show(12000);assert(t.Visible(12000));assert(!t.Visible(22000));}
