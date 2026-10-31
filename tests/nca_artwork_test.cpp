#include "nca_artwork.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <iostream>
using namespace opennow::shortcut::nca;
Digest TestHash(std::span<const uint8_t> bytes) {
    BCRYPT_ALG_HANDLE provider{};BCRYPT_HASH_HANDLE hash{};
    assert(BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
    DWORD len=0,actual=0;assert(BCryptGetProperty(provider,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&len),sizeof(len),&actual,0)>=0);
    Bytes obj(len);Digest out;
    assert(BCryptCreateHash(provider,&hash,obj.data(),obj.size(),nullptr,0,0)>=0);
    assert(BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),bytes.size(),0)>=0);
    assert(BCryptFinishHash(hash,out.data(),out.size(),0)>=0);
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(provider,0);return out;
}
Bytes Read(std::string p){std::ifstream f(p,std::ios::binary);assert(f);return Bytes(std::istreambuf_iterator<char>(f),{});}
void Save(std::string p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());assert(f.good());}
template<class F> void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(int argc,char** argv) {
    assert(argc==2);std::string dir=argv[1];const uint64_t title=0x0100000000001000ULL;
    const auto control=Read(dir+"/control.dec"),meta=Read(dir+"/meta.dec"),database=Read(dir+"/database.bin"),icon=Read(dir+"/new.jpg");
    const auto old_control_hash=TestHash(Read(dir+"/control.raw")),old_meta_hash=TestHash(Read(dir+"/meta.raw"));
    auto updated=ReplaceControlIcon(control,title,icon,TestHash);
    assert(updated!=control);
    // Rebuilding again validates the complete new IVFC chain, and is stable.
    assert(ReplaceControlIcon(updated,title,icon,TestHash)==updated);
    const auto control_hash=TestHash(updated);
    auto updated_meta=ReplaceMetaControl(meta,title,std::span(old_control_hash).first<16>(),control_hash,updated.size(),TestHash);
    assert(updated_meta!=meta);
    assert(ReplaceMetaControl(updated_meta,title,std::span(control_hash).first<16>(),control_hash,updated.size(),TestHash)==updated_meta);
    const auto meta_hash=TestHash(updated_meta);
    auto updated_db=ReplaceDatabaseRecords(database,std::span(old_control_hash).first<16>(),control_hash,updated.size(),std::span(old_meta_hash).first<16>(),meta_hash,updated_meta.size());
    assert(updated_db.size()==database.size() && updated_db!=database);
    assert(std::equal(database.begin(),database.begin()+24,updated_db.begin()));
    // Program record remains byte-for-byte intact (second record in fixture).
    assert(std::equal(database.begin()+48,database.begin()+72,updated_db.begin()+48));
    Reject([&]{ReplaceControlIcon(control,title+1,icon,TestHash);});
    Reject([&]{ReplaceControlIcon({},title,icon,TestHash);});
    auto damaged=control;damaged[0xC00]^=1;
    Reject([&]{ReplaceControlIcon(damaged,title,icon,TestHash);});
    damaged=control;damaged[0x404]=3;
    Reject([&]{ReplaceControlIcon(damaged,title,icon,TestHash);});
    auto wrong=old_control_hash;wrong[0]^=1;
    Reject([&]{ReplaceMetaControl(meta,title,std::span(wrong).first<16>(),control_hash,updated.size(),TestHash);});
    Reject([&]{ReplaceDatabaseRecords({},std::span(old_control_hash).first<16>(),control_hash,updated.size(),std::span(old_meta_hash).first<16>(),meta_hash,updated_meta.size());});
    Save(dir+"/updated-control.dec",updated);Save(dir+"/updated-meta.dec",updated_meta);Save(dir+"/updated-database.bin",updated_db);
    std::cout<<"PASS: real hacBrewPack Control/Meta fixtures, integrity rebuild, DB preservation, malformed input rejection\n";
}
