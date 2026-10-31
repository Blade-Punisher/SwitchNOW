#include "advertisement_source.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
int main() {
    namespace fs=std::filesystem;
    using opennow::AdvertisementSource;
    const auto root=fs::temp_directory_path()/"switchnow-ad-source-test";
    fs::create_directories(root);
    const std::string prefix=(root/"ad").string();
    std::unordered_map<std::string,std::string> fixtures;
    std::vector<std::string> requested;
    auto download=[&](const std::string& url,const std::string& path){
        requested.push_back(url);std::ofstream file(path,std::ios::binary);file<<fixtures.at(url);
        return url;
    };
    fixtures["https://cdn.test/video"]=std::string("binary-video");
    {
        AdvertisementSource source(prefix,download,[]{return false;},{});
        const auto path=source.Prepare("https://cdn.test/video");assert(fs::exists(path));
    }
    assert(fs::is_empty(root));
    fixtures["https://cdn.test/vast"]="<VAST><Ad><InLine><Creatives><Creative><Linear><MediaFiles>"
        "<MediaFile type='video/webm'>https://cdn.test/other</MediaFile>"
        "<MediaFile type='video/mp4'><![CDATA[https://cdn.test/video]]></MediaFile>"
        "</MediaFiles></Linear></Creative></Creatives></InLine></Ad></VAST>";
    fixtures["https://cdn.test/wrapper"]="<VAST><Ad><Wrapper><VASTAdTagURI>vast</VASTAdTagURI></Wrapper></Ad></VAST>";
    {
        AdvertisementSource source(prefix,download,[]{return false;},{});
        source.Prepare("https://cdn.test/wrapper");assert(requested.back()=="https://cdn.test/video");
    }
    assert(fs::is_empty(root));
    fixtures["https://cdn.test/list"]="#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-KEY:METHOD=AES-128,URI=\"key?k=1&amp=2\"\n#EXTINF:4,\nseg.ts\n#EXT-X-BYTERANGE:2@0\nseg.ts\n#EXT-X-ENDLIST\n";
    fixtures["https://cdn.test/key?k=1&amp=2"]="key";
    fixtures["https://cdn.test/seg.ts"]="segment";
    requested.clear();
    {
        AdvertisementSource source(prefix,download,[]{return false;},{});
        const auto path=source.Prepare("https://cdn.test/list");
        std::ifstream file(path);std::string text((std::istreambuf_iterator<char>(file)),{});
        assert(text.find("file:")!=std::string::npos);
        assert(text.find("https://")==std::string::npos);
        assert(requested.size()==3); // Repeated byte-range source keeps one local URI.
    }
    assert(fs::is_empty(root));
    fixtures["https://cdn.test/cycle"]="<VAST><Ad><Wrapper><VASTAdTagURI>cycle</VASTAdTagURI></Wrapper></Ad></VAST>";
    fixtures["https://cdn.test/html"]="<html><body>Not video</body></html>";
    fixtures["https://cdn.test/empty"]="";
    for(const auto name:{"cycle","html","empty"}) {
        bool failed=false;
        try {AdvertisementSource source(prefix,download,[]{return false;},{});source.Prepare(std::string("https://cdn.test/")+name);}
        catch(const std::runtime_error&){failed=true;}
        assert(failed);assert(fs::is_empty(root));
    }
    fixtures["https://cdn.test/nested/wrapper"]="<VAST><Ad><Wrapper><VASTAdTagURI>../vast?x=1&amp;y=2</VASTAdTagURI></Wrapper></Ad></VAST>";
    fixtures["https://cdn.test/vast?x=1&y=2"]=fixtures["https://cdn.test/vast"];
    {
        AdvertisementSource source(prefix,download,[]{return false;},{});
        source.Prepare("https://cdn.test/nested/wrapper");
        assert(requested.back()=="https://cdn.test/video");
    }
    assert(fs::is_empty(root));
    fixtures["https://cdn.test/redirect"]="#EXTM3U\n#EXTINF:1,\nsegment.ts\n#EXT-X-ENDLIST\n";
    fixtures["https://other.test/path/segment.ts"]="segment";
    {
        AdvertisementSource source(prefix,[&](const std::string& url,const std::string& path){
            download(url,path);
            return url=="https://cdn.test/redirect" ? "https://other.test/path/list.m3u8?token=1" : url;
        },[]{return false;},{});
        source.Prepare("https://cdn.test/redirect");
        assert(requested.back()=="https://other.test/path/segment.ts");
    }
    assert(fs::is_empty(root));
    bool partial_failed=false;
    try {
        AdvertisementSource source(prefix,[](const std::string&,const std::string& path)->std::string {
            {std::ofstream file(path);file<<"partial";}
            throw std::runtime_error("Network interrupted");
        },[]{return false;},{});
        source.Prepare("https://cdn.test/video");
    } catch(const std::runtime_error&){partial_failed=true;}
    assert(partial_failed && fs::is_empty(root));
    bool cancelled=false;
    try {AdvertisementSource source(prefix,download,[]{return true;},{});source.Prepare("https://cdn.test/video");}
    catch(const std::runtime_error&){cancelled=true;}
    assert(cancelled && fs::is_empty(root));
    fs::remove(root);
    return 0;
}
