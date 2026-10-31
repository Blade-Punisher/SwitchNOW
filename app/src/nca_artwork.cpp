#include "nca_artwork.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <source_location>
namespace opennow::shortcut::nca {
namespace {
constexpr size_t limit = 4 * 1024 * 1024;
void Require(bool ok, std::source_location location=std::source_location::current()) { if (!ok) throw std::runtime_error("Unsupported or damaged SwitchNOW control metadata at " + std::to_string(location.line())); }
void Range(const Bytes& b, uint64_t at, uint64_t len) { Require(at <= b.size() && len <= b.size() - at); }
uint64_t Read(const Bytes& b, size_t at, size_t count) {
    Range(b, at, count); uint64_t v=0;
    for (size_t i=0;i<count;++i) v |= uint64_t(b[at+i]) << (8*i);
    return v;
}
void Write(Bytes& b, size_t at, uint64_t value, size_t count) {
    Range(b,at,count); for(size_t i=0;i<count;++i) b[at+i]=uint8_t(value>>(8*i));
}
size_t Align(size_t n, size_t a) { Require(n <= limit && a && a <= 0x10000); return (n+a-1)&~(a-1); }
Bytes Slice(const Bytes& b,size_t at,size_t len) {Range(b,at,len);return Bytes(b.begin()+at,b.begin()+at+len);}
void Put(Bytes& b,size_t at,std::span<const uint8_t> data) {Range(b,at,data.size());std::copy(data.begin(),data.end(),b.begin()+at);}
Digest HashRange(const Bytes& b,size_t at,size_t len,const Hash& hash) {Range(b,at,len);return hash(std::span(b).subspan(at,len));}
void CheckHash(const Bytes& b,size_t at,const Digest& digest) {Range(b,at,32);Require(std::equal(digest.begin(),digest.end(),b.begin()+at));}
struct Section {size_t begin,size;};
Section Validate(const Bytes& b,uint64_t title,uint8_t type,const Hash& hash) {
    Require(b.size()>=0xC00 && b.size()<=limit && Read(b,0x200,4)==0x3341434e);
    Require(b[0x205]==type && Read(b,0x210,8)==title && Read(b,0x208,8)==b.size());
    Require(std::all_of(b.begin()+0x230,b.begin()+0x240,[](auto v){return v==0;}));
    Require(b[0x404]==1); // Never rewrite encrypted or retail sections.
    for(size_t i=1;i<4;++i) Require(Read(b,0x240+i*16,8)==0);
    CheckHash(b,0x280,HashRange(b,0x400,0x200,hash));
    const size_t begin=Read(b,0x240,4)*0x200,end=Read(b,0x244,4)*0x200;
    Require(begin>=0xC00 && end==b.size() && begin<end);
    return {begin,end-begin};
}
Bytes HashBlocks(const Bytes& data,size_t block,const Hash& hash,bool pad) {
    Require(block>=32 && block<=0x10000 && (block&(block-1))==0);
    Bytes hashes;
    for(size_t pos=0;pos<data.size();pos+=block) {
        auto chunk=Slice(data,pos,std::min(block,data.size()-pos));
        if(pad) chunk.resize(block,0);
        const auto digest=hash(chunk); hashes.insert(hashes.end(),digest.begin(),digest.end());
    }
    return hashes;
}
Bytes RepackRomfs(const Bytes& old,const Bytes& jpeg) {
    Require(Read(old,0,8)==0x50);
    const auto old_data=Read(old,0x48,8); Range(old,old_data,0);
    Bytes out(0x50,0); Write(out,0,0x50,8);
    for(size_t field=8;field<=0x38;field+=16) {
        const auto at=Read(old,field,8),size=Read(old,field+8,8);
        Range(old,at,size); out.resize(Align(out.size(),8));
        Write(out,field,out.size(),8); Write(out,field+8,size,8);
        out.insert(out.end(),old.begin()+at,old.begin()+at+size);
    }
    out.resize(Align(out.size(),16)); const auto data=out.size(); Write(out,0x48,data,8);
    const auto table=Read(out,0x38,8),table_size=Read(out,0x40,8);
    size_t pos=0,icons=0,nacps=0;
    while(pos<table_size) {
        const size_t entry=table+pos; Range(out,entry,32);
        Require(Read(out,entry,4)==0); // Flat control RomFS only.
        const auto name_len=Read(out,entry+28,4); Require(name_len && name_len<=128);
        Require(pos+32+name_len<=table_size); Range(out,entry+32,name_len);
        const std::string name(reinterpret_cast<const char*>(out.data()+entry+32),name_len);
        const auto offset=Read(out,entry+8,8),size=Read(out,entry+16,8);
        Range(old,old_data,offset); Range(old,old_data+offset,size);
        auto bytes=Slice(old,old_data+offset,size);
        if(name=="control.nacp") {
            Require(size==0x4000);
            Require(std::memcmp(bytes.data()+0x200,"SwitchNOW",10)==0);
            ++nacps;
        } else if(name.starts_with("icon_") && name.ends_with(".dat")) {
            bytes=jpeg; ++icons;
        } else Require(false);
        out.resize(Align(out.size(),16));
        Write(out,entry+8,out.size()-data,8); Write(out,entry+16,bytes.size(),8);
        out.insert(out.end(),bytes.begin(),bytes.end()); Require(out.size()<=limit);
        pos+=Align(32+name_len,4);
    }
    Require(pos==table_size && nacps==1 && icons>=1 && icons<=16);
    return out;
}
Bytes Finish(Bytes header,const Bytes& section,const Section& old,const Hash& hash) {
    header.resize(old.begin); header.insert(header.end(),section.begin(),section.end());
    header.resize(Align(header.size(),0x200));
    Write(header,0x208,header.size(),8); Write(header,0x244,header.size()/0x200,4);
    Put(header,0x280,HashRange(header,0x400,0x200,hash));
    // The original forwarder is already unsigned homebrew; no signature forgery.
    std::fill(header.begin(),header.begin()+0x200,0);
    return header;
}
}
Bytes ReplaceControlIcon(const Bytes& b,uint64_t title,const Bytes& jpeg,const Hash& hash) {
    Require(jpeg.size()>=4 && jpeg.size()<0x20000 && jpeg[0]==0xff && jpeg[1]==0xd8);
    const auto section=Validate(b,title,2,hash);
    Require(b[0x402]==0 && b[0x403]==3 && Read(b,0x408,4)==0x43465649);
    Require(Read(b,0x410,4)==32 && Read(b,0x414,4)==7);
    std::array<Bytes,6> levels; std::array<size_t,6> blocks;
    for(size_t i=0;i<6;++i) {
        const auto at=0x418+i*24,off=Read(b,at,8),size=Read(b,at+8,8),order=Read(b,at+16,4);
        Require(order>=5 && order<=16 && off<=section.size && size<=section.size-off && size>0);
        levels[i]=Slice(b,section.begin+off,size); blocks[i]=size_t(1)<<order;
    }
    const auto master=HashBlocks(levels[0],blocks[0],hash,true);
    Require(master.size()==32); Range(b,0x4C8,32);
    Require(std::equal(master.begin(),master.end(),b.begin()+0x4C8));
    for(size_t i=1;i<6;++i) {
        const auto expected=HashBlocks(levels[i],blocks[i],hash,true);
        Require(expected.size()<=levels[i-1].size());
        Require(std::equal(expected.begin(),expected.end(),levels[i-1].begin()));
        Require(std::all_of(levels[i-1].begin()+expected.size(),levels[i-1].end(),[](auto v){return v==0;}));
    }
    levels[5]=RepackRomfs(levels[5],jpeg);
    levels[5].resize(Align(levels[5].size(),blocks[5]));
    for(int i=4;i>=0;--i) {
        levels[i]=HashBlocks(levels[i+1],blocks[i+1],hash,true);
        levels[i].resize(Align(levels[i].size(),blocks[i]));
    }
    Bytes header=b, rebuilt;
    for(size_t i=0;i<6;++i) {
        rebuilt.resize(Align(rebuilt.size(),blocks[i]));
        Write(header,0x418+i*24,rebuilt.size(),8);
        Write(header,0x420+i*24,levels[i].size(),8);
        rebuilt.insert(rebuilt.end(),levels[i].begin(),levels[i].end());
    }
    const auto new_master=HashBlocks(levels[0],blocks[0],hash,true);
    Require(new_master.size()==32); Put(header,0x4C8,new_master);
    return Finish(std::move(header),rebuilt,section,hash);
}
Bytes ReplaceMetaControl(const Bytes& b,uint64_t title,std::span<const uint8_t,16> old_control,
                         const Digest& control_hash,uint64_t control_size,const Hash& hash) {
    const auto section=Validate(b,title,1,hash);
    Require(b[0x402]==1 && b[0x403]==2);
    const size_t block=Read(b,0x428,4),table_off=Read(b,0x430,8),table_size=Read(b,0x438,8);
    const size_t pfs_off=Read(b,0x440,8),pfs_size=Read(b,0x448,8);
    Require(table_off<=section.size && table_size<=section.size-table_off);
    Require(pfs_off<=section.size && pfs_size<=section.size-pfs_off);
    auto pfs=Slice(b,section.begin+pfs_off,pfs_size);
    const auto old_hashes=HashBlocks(pfs,block,hash,false);
    Require(old_hashes.size()==table_size);
    Require(old_hashes==Slice(b,section.begin+table_off,table_size));
    CheckHash(b,0x408,hash(old_hashes));
    Require(Read(pfs,0,4)==0x30534650 && Read(pfs,4,4)==1);
    const auto strings=Read(pfs,8,4),file_offset=Read(pfs,0x10,8),file_size=Read(pfs,0x18,8);
    const size_t data_offset=0x28+strings;
    Range(pfs,data_offset,file_offset); Range(pfs,data_offset+file_offset,file_size);
    const size_t cnmt=data_offset+file_offset;
    Require(file_size>=0x40 && Read(pfs,cnmt,8)==title && pfs[cnmt+0xc]==0x80);
    const size_t extended=Read(pfs,cnmt+0xe,2),count=Read(pfs,cnmt+0x10,2);
    Require(count>0 && count<=8 && 0x20+extended+count*0x38+0x20<=file_size);
    // Our bundled hacBrewPack emits a zero CNMT digest. Accept that legacy
    // format only after the NCA section hashes have been validated above.
    if(!std::all_of(pfs.begin()+cnmt+file_size-32,pfs.begin()+cnmt+file_size,[](auto v){return v==0;}))
        CheckHash(pfs,cnmt+file_size-32,HashRange(pfs,cnmt,file_size-32,hash));
    size_t replaced=0;
    for(size_t i=0;i<count;++i) {
        const size_t record=cnmt+0x20+extended+i*0x38;
        if(pfs[record+0x36]!=3) continue;
        Require(std::equal(old_control.begin(),old_control.end(),pfs.begin()+record+0x20));
        Put(pfs,record,control_hash); Put(pfs,record+0x20,std::span(control_hash).first(16));
        Require(control_size<limit); Write(pfs,record+0x30,control_size,5); ++replaced;
    }
    Require(replaced==1);
    Put(pfs,cnmt+file_size-32,HashRange(pfs,cnmt,file_size-32,hash));
    Bytes out=b;
    Put(out,section.begin+pfs_off,pfs);
    const auto hashes=HashBlocks(pfs,block,hash,false);
    Put(out,section.begin+table_off,hashes); Put(out,0x408,hash(hashes));
    Put(out,0x280,HashRange(out,0x400,0x200,hash));
    std::fill(out.begin(),out.begin()+0x200,0);
    return out;
}
Bytes ReplaceDatabaseRecords(const Bytes& b,std::span<const uint8_t,16> old_control,
    const Digest& control_hash,uint64_t control_size,std::span<const uint8_t,16> old_meta,
    const Digest& meta_hash,uint64_t meta_size) {
    const size_t extended=Read(b,0,2),count=Read(b,2,2),base=8+extended;
    Require(count>0 && count<=8); Range(b,base,count*24);
    Bytes out=b; size_t controls=0,metas=0;
    for(size_t i=0;i<count;++i) {
        const size_t entry=base+i*24; const auto type=b[entry+22];
        if(type!=0 && type!=3) continue;
        const auto old=type==3?old_control:old_meta;
        Require(std::equal(old.begin(),old.end(),b.begin()+entry));
        const auto& digest=type==3?control_hash:meta_hash;
        const auto size=type==3?control_size:meta_size; Require(size<limit);
        Put(out,entry,std::span(digest).first(16)); Write(out,entry+16,size,5);
        if(type==3) ++controls; else ++metas;
    }
    Require(controls==1 && metas==1); return out;
}
}
