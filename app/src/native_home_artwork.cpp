#include "native_home_artwork.hpp"
#include "nca_artwork.hpp"
#include "home_artwork_transaction.hpp"
#include "home_record_policy.hpp"
#include "runtime_journal.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#ifdef __SWITCH__
#include <switch.h>
namespace opennow::shortcut {
namespace {
using nca::Bytes;
using nca::Digest;
void Check(Result rc,const char* stage) {
    if(R_FAILED(rc)) { char message[160]; snprintf(message,sizeof(message),"%s: 0x%08X",stage,rc); throw std::runtime_error(message); }
}
Digest Hash(std::span<const uint8_t> bytes) {Digest out;sha256CalculateHash(out.data(),bytes.data(),bytes.size());return out;}
struct Keys {
    std::array<uint8_t,32> header{};
    ~Keys(){volatile uint8_t* p=header.data();for(size_t i=0;i<header.size();++i)p[i]=0;}
};
int Hex(char c) {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
void LoadKeys(Keys& keys) {
    std::ifstream file("sdmc:/switch/prod.keys"); std::string line;
    while(std::getline(file,line)) {
        const auto eq=line.find('='); if(eq==std::string::npos)continue;
        auto name=line.substr(0,eq);name.erase(std::remove_if(name.begin(),name.end(),[](char c){return c==' '||c=='\t';}),name.end());
        if(name!="header_key")continue;
        auto text=line.substr(eq+1);text.erase(std::remove_if(text.begin(),text.end(),[](char c){return c==' '||c=='\t'||c=='\r';}),text.end());
        if(text.size()!=64)throw std::runtime_error("Invalid NCA header key length");
        for(size_t i=0;i<32;++i){int hi=Hex(text[2*i]),lo=Hex(text[2*i+1]);if(hi<0||lo<0)throw std::runtime_error("Invalid NCA header key");keys.header[i]=(hi<<4)|lo;}
        std::fill(text.begin(),text.end(),0);std::fill(line.begin(),line.end(),0);return;
    }
    constexpr uint8_t kek_source[16]={0x1f,0x12,0x91,0x3a,0x4a,0xcb,0xf0,0x0d,0x4c,0xde,0x3a,0xf6,0xd5,0x23,0x88,0x2a};
    constexpr uint8_t key_source[32]={
    0x5a, 0x3e, 0xd8, 0x4f, 0xde, 0xc0, 0xd8, 0x26,
    0x31, 0xf7, 0xe2, 0x5d, 0x19, 0x7b, 0xf5, 0xd0,
    0x1c, 0x9b, 0x7b, 0xfa, 0xf6, 0x28, 0x18, 0x3d,
    0x71, 0xf6, 0x4d, 0x73, 0xf1, 0x50, 0xb9, 0xd2,
  };
    Check(splCryptoInitialize(),"spl_initialize");
    uint8_t kek[16]{};
    Result rc=splCryptoGenerateAesKek(kek_source,0,0,kek);
    if(R_SUCCEEDED(rc))rc=splCryptoGenerateAesKey(kek,key_source,keys.header.data());
    if(R_SUCCEEDED(rc))rc=splCryptoGenerateAesKey(kek,key_source+16,keys.header.data()+16);
    splCryptoExit(); std::fill(std::begin(kek),std::end(kek),0); Check(rc,"derive_header_key");
}
void CryptHeader(Bytes& b,const Keys& keys,bool encrypt) {
    if(b.size()<0xC00)throw std::runtime_error("NCA header truncated");
    Aes128XtsContext ctx;
    aes128XtsContextCreate(&ctx,keys.header.data(),keys.header.data()+16,encrypt);
    std::array<uint8_t,0x200> sector{};
    for(size_t i=0;i<6;++i) {
        aes128XtsContextResetSector(&ctx,i,true);
        size_t size=encrypt ? aes128XtsEncrypt(&ctx,sector.data(),b.data()+i*0x200,0x200)
                            : aes128XtsDecrypt(&ctx,sector.data(),b.data()+i*0x200,0x200);
        if(size!=0x200)throw std::runtime_error("NCA header cipher failed");
        std::copy(sector.begin(),sector.end(),b.begin()+i*0x200);
    }
}
struct Services {
    bool ns=false,ncm=false;Service manager{};NcmContentStorage storage{};NcmContentMetaDatabase database{};
    ~Services(){serviceClose(&manager);ncmContentMetaDatabaseClose(&database);ncmContentStorageClose(&storage);if(ncm)ncmExit();if(ns)nsExit();}
    void Open(){Check(nsInitialize(),"ns_initialize");ns=true;Check(nsGetApplicationManagerInterface(&manager),"ns_application_manager");Check(ncmInitialize(),"ncm_initialize");ncm=true;Check(ncmOpenContentStorage(&storage,NcmStorageId_SdCard),"open_sd_content");Check(ncmOpenContentMetaDatabase(&database,NcmStorageId_SdCard),"open_sd_metadata");}
    void Invalidate(uint64_t id){
        Check(serviceDispatchIn(&manager,404,id),"invalidate_home_icon");
    }
    void Notify(uint64_t id){
        // R1 TouchApplication did not invalidate qlaunch's visible tile on
        // hardware. Republish the existing installed record, without deleting
        // it or changing any of its content keys/storage associations.
        const auto records=ReadRecords(id);
        ValidateHomeRecords(records,id);
        if(ReadRecords(id)!=records)throw std::runtime_error("HOME record changed before notification");
        const struct { u8 event;u8 padding[7];u64 title; } in{3,{},id};
        Check(serviceDispatchIn(&manager,16,in,
            .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_In},
            .buffers={{records.data(),records.size()}}),"republish_home_record");
        LogRuntimeEvent("shortcut","home_tile_refresh_requested","method=installed_record; visible_tile_unverified");
    }
    Bytes ReadRecords(uint64_t id){
        static_assert(sizeof(NcmContentMetaKey)==16);
        Bytes records;
        for(u64 offset=0;offset<=256;offset+=16) {
            std::array<uint8_t,24*16> page{};s32 count=0;
            const struct {u64 offset;u64 title;} in{offset,id};
            Check(serviceDispatchInOut(&manager,17,in,count,
                .buffer_attrs={SfBufferAttr_HipcMapAlias|SfBufferAttr_Out},
                .buffers={{page.data(),page.size()}}),"list_home_records");
            if(count<0 || count>16 || offset+count>256)
                throw std::runtime_error("Unexpected HOME record count");
            records.insert(records.end(),page.begin(),page.begin()+count*24);
            if(count<16)return records;
        }
        throw std::runtime_error("Too many HOME records");
    }
};
Bytes ReadContent(Services& s,const NcmContentId& id) {
    s64 size=0;Check(ncmContentStorageGetSizeFromContentId(&s.storage,&size,&id),"content_size");
    if(size<0xC00 || size>4*1024*1024)throw std::runtime_error("Unexpected shortcut content size");
    Bytes out(size);Check(ncmContentStorageReadContentIdFile(&s.storage,out.data(),out.size(),&id,0),"read_content");
    const auto digest=Hash(out);if(!std::equal(id.c,id.c+16,digest.begin()))throw std::runtime_error("Installed NCA content hash mismatch");
    return out;
}
NcmContentId StoreContent(Services& s,const Bytes& bytes) {
    const auto digest=Hash(bytes);NcmContentId id{};std::copy_n(digest.begin(),16,id.c);
    bool exists=false;Check(ncmContentStorageHas(&s.storage,&exists,&id),"content_exists");
    if(!exists) {
        NcmPlaceHolderId placeholder{};Check(ncmContentStorageGeneratePlaceHolderId(&s.storage,&placeholder),"create_placeholder_id");
        Check(ncmContentStorageCreatePlaceHolder(&s.storage,&id,&placeholder,bytes.size()),"create_placeholder");
        try {
            for(size_t pos=0;pos<bytes.size();pos+=0x10000)
                Check(ncmContentStorageWritePlaceHolder(&s.storage,&placeholder,pos,bytes.data()+pos,std::min<size_t>(0x10000,bytes.size()-pos)),"write_placeholder");
            Check(ncmContentStorageFlushPlaceHolder(&s.storage),"flush_placeholder");
            Check(ncmContentStorageRegister(&s.storage,&id,&placeholder),"register_content");
        }catch(...){ncmContentStorageDeletePlaceHolder(&s.storage,&placeholder);throw;}
    }
    if(ReadContent(s,id)!=bytes)throw std::runtime_error("New NCA read-back mismatch");
    return id;
}
Bytes ReadDatabase(Services& s,const NcmContentMetaKey& key) {
    u64 size=0;Check(ncmContentMetaDatabaseGetSize(&s.database,&size,&key),"metadata_size");
    if(size<8||size>65536)throw std::runtime_error("Invalid shortcut metadata size");
    Bytes bytes(size);u64 actual=0;Check(ncmContentMetaDatabaseGet(&s.database,&key,&actual,bytes.data(),bytes.size()),"read_metadata");
    if(actual!=size)throw std::runtime_error("Metadata read-back size mismatch");
    return bytes;
}
bool IconEquals(uint64_t title,const Bytes& jpeg,NsApplicationControlSource source) {
    auto control=std::make_unique<NsApplicationControlData>();u64 actual=0;
    Check(nsGetApplicationControlData(source,title,control.get(),sizeof(*control),&actual),
        source==NsApplicationControlSource_StorageOnly?"read_installed_home_control":"read_cached_home_control");
    return actual==sizeof(NacpStruct)+jpeg.size() && std::equal(jpeg.begin(),jpeg.end(),control->icon);
}
bool VerifyAndNotify(Services& services,uint64_t title,const Bytes& jpeg) {
    // Storage populates an empty cache; StorageOnly deliberately bypasses it.
    return VerifyHomeArtworkAndNotify(
        [&]{return IconEquals(title,jpeg,NsApplicationControlSource_StorageOnly);},
        [&]{return IconEquals(title,jpeg,NsApplicationControlSource_Storage);},
        [&]{services.Notify(title);});
}
}
bool UpdateInstalledHomeIcon(const std::string& nro_path,const std::vector<uint8_t>& jpeg) {
    try {
        if(!nro_path.starts_with("sdmc:/switch/SwitchNOW Shortcuts/") || nro_path.find("/../")!=std::string::npos)
            throw std::runtime_error("Invalid shortcut provenance");
        const std::string identity=nro_path+"\""+nro_path+"\"";
        auto digest=Hash(std::span(reinterpret_cast<const uint8_t*>(identity.data()),identity.size()));
        uint64_t title=0;for(int i=7;i>=0;--i)title=(title<<8)|digest[i];
        title=0x0100000000000000ULL|(title&0x00fffffffffff000ULL);
        Services services;services.Open();
        NcmContentMetaKey key{};
        Result exists=ncmContentMetaDatabaseGetLatestContentMetaKey(&services.database,&key,title);
        if(R_FAILED(exists)){
            char detail[96];snprintf(detail,sizeof(detail),"title=%016llX result=0x%08X",static_cast<unsigned long long>(title),exists);
            LogRuntimeEvent("shortcut","home_icon_not_installed",detail);return false;
        }
        if(key.id!=title || key.type!=NcmContentMetaType_Application || key.install_type!=NcmContentInstallType_Full)
            throw std::runtime_error("Unexpected installed shortcut metadata type");
        if(IconEquals(title,jpeg,NsApplicationControlSource_StorageOnly)) {
            // Previous builds returned here based on cached data and never
            // retried the HOME notification. Repair cache even on HTTP 304.
            services.Invalidate(title);
            VerifyAndNotify(services,title,jpeg);
            LogRuntimeEvent("shortcut","home_icon_cache_resynced");
            return false; // No new artwork: do not repeat the success toast.
        }
        Keys keys;LoadKeys(keys);
        NcmContentId old_control{},old_meta{};
        Check(ncmContentMetaDatabaseGetContentIdByType(&services.database,&old_control,&key,NcmContentType_Control),"control_id");
        Check(ncmContentMetaDatabaseGetContentIdByType(&services.database,&old_meta,&key,NcmContentType_Meta),"meta_id");
        const auto old_database=ReadDatabase(services,key);
        auto control=ReadContent(services,old_control),meta=ReadContent(services,old_meta);
        CryptHeader(control,keys,false);CryptHeader(meta,keys,false);
        auto new_control=nca::ReplaceControlIcon(control,title,jpeg,Hash);
        CryptHeader(new_control,keys,true);const auto control_hash=Hash(new_control);
        auto new_meta=nca::ReplaceMetaControl(meta,title,old_control.c,control_hash,new_control.size(),Hash);
        CryptHeader(new_meta,keys,true);const auto meta_hash=Hash(new_meta);
        const auto new_database=nca::ReplaceDatabaseRecords(old_database,old_control.c,control_hash,new_control.size(),old_meta.c,meta_hash,new_meta.size());
        LogRuntimeEvent("shortcut","home_icon_stage");
        StoreContent(services,new_control);StoreContent(services,new_meta);
        CommitHomeArtwork(old_database,new_database,{
            [&]{return ReadDatabase(services,key);},
            [&](const Bytes& bytes){Check(ncmContentMetaDatabaseSet(&services.database,&key,bytes.data(),bytes.size()),"set_metadata");},
            [&]{Check(ncmContentMetaDatabaseCommit(&services.database),"commit_metadata");},
            [&]{services.Invalidate(title);},
            [&]{return VerifyAndNotify(services,title,jpeg);},
            [&](bool restored){LogRuntimeEvent("shortcut","home_icon_rollback",restored?"restored":"FAILED; previous content retained");}
        });
        // Old Control/Meta NCAs remain available for recovery. Program content
        // is never deleted/reinstalled/relaunched. NS records are republished.
        LogRuntimeEvent("shortcut","home_icon_updated");return true;
    }catch(const std::exception& error){LogRuntimeEvent("shortcut","home_icon_update_failed",error.what());}
    catch(...){LogRuntimeEvent("shortcut","home_icon_update_failed","unknown error");}
    return false;
}
}
#else
namespace opennow::shortcut {bool UpdateInstalledHomeIcon(const std::string&,const std::vector<uint8_t>&){return false;}}
#endif
