#include "image_http_session.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <curl/curl.h>
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
    assert(argc==2);curl_global_init(CURL_GLOBAL_DEFAULT);
    {
        opennow::ImageHttpSession session;
        const auto first=session.Fetch(argv[1],6*1024*1024,[]{return false;});
        assert(first.status==200&&!first.body.empty());
        int width=0,height=0,channels=0;
        assert(stbi_info_from_memory(reinterpret_cast<const unsigned char*>(first.body.data()),static_cast<int>(first.body.size()),&width,&height,&channels));
        assert(width>0&&width<=1120&&height>0&&height<=4096);
        const auto second=session.Fetch(argv[1],6*1024*1024,[]{return false;});
        assert(second.status==200&&second.body==first.body);
        std::cout<<"Live NVIDIA screenshot: "<<width<<"x"<<height<<", bytes="<<first.body.size()<<", new_connections="<<first.new_connections<<"/"<<second.new_connections<<", host_download_ms="<<first.elapsed_ms<<"/"<<second.elapsed_ms<<".\n";
    }
    curl_global_cleanup();
}
