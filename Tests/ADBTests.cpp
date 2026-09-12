#include "ADB/Client.hpp"
#include "ADB/RSAPublicKey.hpp"
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace emu::adb;
namespace {
constexpr auto CNXN=command('C','N','X','N'), AUTH=command('A','U','T','H'), OPEN=command('O','P','E','N');
constexpr auto OKAY=command('O','K','A','Y'), WRTE=command('W','R','T','E'), CLSE=command('C','L','S','E');
void check(bool value) { if (!value) throw std::runtime_error("ADB protocol regression"); }
uint32_t get(const std::vector<uint8_t>& b,size_t at) { return uint32_t(b[at])|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24; }
struct Guest {
    std::deque<uint8_t> rx;
    std::vector<uint8_t> tx, sync, file;
    bool syncing=false, cancel=false, staleCloseOnWrite=false;
    uint32_t local=0;
    unsigned signatures=0, publicKeys=0;
    void reply(Packet p) { auto data=encode(p); rx.insert(rx.end(),data.begin(),data.end()); }
    void packet(const Packet& p) {
        if (p.command==CNXN) reply({AUTH,1,0,std::vector<uint8_t>(20,42)});
        else if (p.command==AUTH) {
            if (p.arg0==2) { check(p.payload==std::vector<uint8_t>(256,7)); signatures++; reply({AUTH,1,0,std::vector<uint8_t>(20,43)}); }
            else { check(p.arg0==3 && p.payload.back()==0); publicKeys++; reply({CNXN,0x01000000,4096,{}}); }
        } else if (p.command==OPEN) {
            check(p.payload.back()==0); local=p.arg0; reply({OKAY,99,local,{}});
            syncing=std::string(p.payload.begin(),p.payload.end()-1)=="sync:";
            if (!syncing) { reply({WRTE,99,local,{'h','e','l','l','o','\n'}}); reply({CLSE,99,local,{}}); }
        } else if (p.command==WRTE) {
            check(syncing && p.arg0==local && p.arg1==99);
            if (staleCloseOnWrite) { reply({CLSE,99,local-1,{}}); staleCloseOnWrite=false; }
            reply({OKAY,99,local,{}}); sync.insert(sync.end(),p.payload.begin(),p.payload.end());
            while (sync.size()>=8) {
                auto cmd=get(sync,0), length=get(sync,4);
                if (cmd==command('D','O','N','E')) {
                    check(length==0); sync.erase(sync.begin(),sync.begin()+8);
                    reply({WRTE,99,local,{'O','K','A','Y',0,0,0,0}}); break;
                }
                if (sync.size()<8+length) break;
                if (cmd==command('D','A','T','A')) file.insert(file.end(),sync.begin()+8,sync.begin()+8+length);
                else check(cmd==command('S','E','N','D'));
                sync.erase(sync.begin(),sync.begin()+8+length);
            }
        } else if (p.command==CLSE && syncing) {
            // Delayed close from the completed sync stream can arrive after
            // the next OPEN. It must not poison the next transfer.
            reply({CLSE,99,p.arg0,{}}); syncing=false;
        } else check(p.command==OKAY || p.command==CLSE);
    }
    Transport transport() {
        return {[]{return true;},[this]{return cancel;},
            [this](std::span<uint8_t> b) { size_t n=std::min({size_t(7),rx.size(),b.size()}); for (size_t i=0;i<n;++i) {b[i]=rx.front();rx.pop_front();} return n; },
            [this](std::span<const uint8_t> b) {
                size_t n=std::min<size_t>(11,b.size()); tx.insert(tx.end(),b.begin(),b.begin()+n);
                while (tx.size()>=24 && tx.size()>=24+get(tx,12)) { size_t count=24+get(tx,12); auto p=decode(std::span(tx).first(count)); tx.erase(tx.begin(),tx.begin()+count); packet(p); }
                return n;
            }, [](std::span<const uint8_t> token) {check(token.size()==20); return std::vector<uint8_t>(256,7);},
            []{return std::vector<uint8_t>{'k','e','y',0};}};
    }
};
}
int main() {
    try {
        Guest guest; Client client(guest.transport()); client.connect();
        check(guest.signatures==1 && guest.publicKeys==1);
        check(client.shell("echo hello")=="hello\n");
        std::string name="/tmp/androidemu-adb-test-"+std::to_string(getpid());
        std::vector<uint8_t> apk(17001); for (size_t i=0;i<apk.size();++i) apk[i]=uint8_t(i);
        {std::ofstream f(name,std::ios::binary);f.write(reinterpret_cast<const char *>(apk.data()),apk.size());}
        uint64_t progress=0;
        try { client.push(name,"/data/local/tmp/test.apk",[&](uint64_t n,uint64_t total){check(n>=progress && total==apk.size());progress=n;}); }
        catch (...) { unlink(name.c_str()); throw; }
        check(guest.file==apk && progress==apk.size());
        check(client.shell("echo after push")=="hello\n");
        guest.file.clear();
        guest.staleCloseOnWrite=true;
        client.push(name,"/data/local/tmp/second.apk");
        check(guest.file==apk);
        check(client.shell("echo after second push")=="hello\n");
        unlink(name.c_str());
        guest.cancel=true; bool cancelled=false;
        try { client.shell("echo blocked"); } catch (const std::exception&) {cancelled=true;} check(cancelled);
        std::array<uint8_t,256> modulus; modulus.fill(255);
        auto key=androidPublicKey(modulus,65537);
        check(key[0]==64 && key[4]==1 && key[264]==1 && key[520]==1 && key[522]==1);
        for (size_t i=265;i<520;++i) check(key[i]==0);
        modulus[255]=0x61; // n=2^2048-159, hence R^2 mod n=159^2=25281.
        key=androidPublicKey(modulus,65537);
        check(key[264]==0xc1 && key[265]==0x62);
        for (size_t i=266;i<520;++i) check(key[i]==0);
        bool rejected=false; modulus[255]=254;
        try { (void)androidPublicKey(modulus,65537); } catch (const std::invalid_argument&) {rejected=true;} check(rejected);
        std::cout<<"ADB AUTH, fragmented transport, shell, sync, cancellation and RSA wire tests passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
