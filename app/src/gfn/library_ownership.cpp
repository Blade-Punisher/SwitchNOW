#include "library_ownership.hpp"
#include <jansson.h>
#include <memory>
#include <cstdlib>
#include <stdexcept>
namespace opennow::gfn::ownership {
using Json=std::unique_ptr<json_t,decltype(&json_decref)>;
std::string RequestBody(const std::string& id,bool owned) {
    if(id.empty() || id.size()>64 || id.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("Game store variant ID is unavailable. Refresh the catalog.");
    Json root(json_object(),json_decref);
    json_object_set_new(root.get(),"operationName",json_string(owned?"AddOwnedVariant":"RemoveOwnedVariant"));
    json_object_set_new(root.get(),"query",json_string(owned?
        "mutation AddOwnedVariant($cmsId: String!, $locale: String!) { addOwnedVariant(language: $locale, variantId: $cmsId) { app { id } } }":
        "mutation RemoveOwnedVariant($cmsId: String!, $locale: String!) { removeOwnedVariant(language: $locale, variantId: $cmsId) { app { id } } }"));
    json_object_set_new(root.get(),"variables",json_pack("{s:s,s:s}","cmsId",id.c_str(),"locale","en_US"));
    char* raw=json_dumps(root.get(),JSON_COMPACT);if(!raw)throw std::runtime_error("Unable to build ownership request");
    std::string result(raw);free(raw);return result;
}
void ValidateResponse(const std::string& body,const std::string& expected,bool owned) {
    Json root(json_loadb(body.data(),body.size(),JSON_REJECT_DUPLICATES,nullptr),json_decref);
    if(!root || expected.empty())throw std::runtime_error("Invalid GFN ownership response");
    auto* errors=json_object_get(root.get(),"errors");
    if(errors && (!json_is_array(errors) || json_array_size(errors)>0))
        throw std::runtime_error("GFN did not confirm changing this game in the library.");
    auto* app=json_object_get(json_object_get(json_object_get(root.get(),"data"),owned?"addOwnedVariant":"removeOwnedVariant"),"app");
    const char* id=json_string_value(json_object_get(app,"id"));
    if(!id || expected!=id)throw std::runtime_error("GFN did not confirm the requested game. Refresh the library before retrying.");
}
}
