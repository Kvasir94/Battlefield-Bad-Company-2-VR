#include "fvr/runtime/TestRange.h"
#include "Test.h"
#include <fstream>
#include <sstream>
#include <string>
using namespace fvr::runtime;
namespace {
std::string Read(const std::string& path){std::ifstream in(path,std::ios::binary);std::ostringstream s;s<<in.rdbuf();return s.str();}
bool Rejects(std::string_view body,std::string_view wanted={}){
    RangeSpec r;std::string error;
    const bool ok=ParseRange(std::string("FVR_BC2_RANGE 1\n")+std::string(body),r,error);
    if(ok){std::fprintf(stderr,"accepted: %.*s\n",int(body.size()),body.data());return false;}
    if(!wanted.empty()&&error.find(wanted)==std::string::npos){std::fprintf(stderr,"error '%s' lacks '%.*s'\n",error.c_str(),int(wanted.size()),wanted.data());return false;}
    return true;
}
constexpr std::string_view Base="level=Levels/sp_02\nweapons=XM8_sp_s\n";
std::string With(std::string_view extra){return std::string(Base)+std::string(extra);}
}
int main(int argc,char** argv){
    // Shared golden files: the Python runner produced them, C++ must agree byte for byte.
    CHECK(argc==2);const std::string root=argv[1];
    for(const char* name:{"sp02-xm8","xm8-spas","aek971-fixed-dt"}){
        RangeSpec r;std::string error;
        const auto text=Read(root+"/tools/test-range/ranges/"+name+".ini");CHECK(!text.empty());
        if(!ParseRange(text,r,error)){std::fprintf(stderr,"%s: %s\n",name,error.c_str());return 1;}
        const auto fixture=root+"/tests/fixtures/test-range/"+name;
        CHECK(FormatRange(r)==Read(fixture+".canonical"));
        std::string settings;for(const auto& s:RangeSettings(r))settings+=s.key+" "+s.value+"\n";
        CHECK(settings==Read(fixture+".settings"));
        RangeSpec again;CHECK(ParseRange(FormatRange(r),again,error));CHECK(again==r);
    }
    {   // Fallback range: manual route, nothing to give, XM8 already in the SP_02 kit.
        RangeSpec r;std::string error;
        CHECK(ParseRange(Read(root+"/tools/test-range/ranges/sp02-xm8.ini"),r,error));
        CHECK(r.route==RangeLevelRoute::Manual&&!r.clearInventory&&r.weapons.size()==1&&r.Selected()=="XM8_sp_s");
        CHECK(r.weaponSlots==std::vector<int>{0});
    }
    {   // Layout tolerance: CRLF, ';' comments, spaces around '=', bracket list, no trailing newline.
        RangeSpec r;std::string error;
        CHECK(ParseRange("; c\r\n\r\nFVR_BC2_RANGE 1\r\n level = Levels/SP_02 \r\nweapons=[XM8_sp_s, SPAS12_sp@Primary2]\r\nammo.SPAS12_sp=7/native",r,error));
        CHECK(r.level=="Levels/SP_02"&&r.weapons.size()==2&&r.weapons[1]=="SPAS12_sp"&&r.weaponSlots[0]==-1&&r.weaponSlots[1]==1);
        CHECK(r.ammoOverrides.size()==1&&r.ammoOverrides[0].loaded==7&&r.ammoOverrides[0].reserve==-1);
        CHECK(r.route==RangeLevelRoute::Menu); // default: route B
        const auto pairs=RangeSettings(r);CHECK(pairs.size()==1&&pairs[0].key=="Client.Level"&&pairs[0].value=="Levels/SP_02");
    }
    {   // Inverted switches and every typed key.
        RangeSpec r;std::string error;
        CHECK(ParseRange("FVR_BC2_RANGE 1\n"+With("cutscenes=true\ncheckpoints=true\nkit=3\nteam=1\nfixed_dt=0.01\nscripted_input=true\n"
            "instant_spawn=true\nunlimited_ammo=false\nfast_startup=true\nset.Client.DisableMenuFlow=true\nteleport=1.5,-2,30.25,90\n"),r,error));
        const auto s=RangeSettings(r);
        const auto value=[&](std::string_view key){for(const auto& p:s)if(p.key==key)return p.value;return std::string("?");};
        CHECK(value("Server.DisableCutscenes")=="false"&&value("Server.DisableCheckpoints")=="false");
        CHECK(value("Client.Kit")=="3"&&value("Client.Team")=="1"&&value("Game.ForceDeltaTime")=="0.01");
        CHECK(value("Client.IsFastStartupEnabled")=="true"&&value("Client.UseScriptedInput")=="true"&&value("Server.InstantSpawn")=="true"&&value("Server.HasUnlimitedAmmo")=="false");
        CHECK(s.back().key=="Client.DisableMenuFlow");
        CHECK(r.teleport&&Near(r.teleport->x,1.5f)&&Near(r.teleport->y,-2)&&Near(r.teleport->yaw,90)&&r.teleport->hasYaw);
        RangeSpec again;CHECK(ParseRange(FormatRange(r),again,error));CHECK(again==r);
    }
    // Rejections, each with the reason the author sees.
    {RangeSpec r;std::string e;CHECK(!ParseRange(std::string(Base),r,e)&&e.find("header")!=std::string::npos);}
    {RangeSpec r;std::string e;CHECK(!ParseRange("\xEF\xBB\xBF""FVR_BC2_RANGE 1\n"+std::string(Base),r,e)&&e.find("BOM")!=std::string::npos);}
    {RangeSpec r;std::string e;CHECK(!ParseRange("FVR_BC2_RANGE 2\n"+std::string(Base),r,e));}
    CHECK(Rejects("weapons=XM8_sp_s\n","missing level"));
    CHECK(Rejects("level=Levels/sp_02\n","missing weapons"));
    CHECK(Rejects(With("color=red\n"),"unknown key color"));
    CHECK(Rejects(With("level=Levels/sp_03\n"),"duplicate key level"));
    CHECK(Rejects("level=Levels/SP_99\nweapons=XM8_sp_s\n"));
    CHECK(Rejects("level=Levels/../sp_02\nweapons=XM8_sp_s\n"));
    CHECK(Rejects("level=sp_02\nweapons=XM8_sp_s\n"));
    CHECK(Rejects("level=Levels/mp_005\nweapons=XM8_sp_s\n"));
    CHECK(Rejects("level=Levels/sp_02\nweapons=XM8 sp\n"));
    CHECK(Rejects("level=Levels/sp_02\nweapons=XM8_sp_s,xm8_SP_S\n","duplicate weapon"));
    CHECK(Rejects("level=Levels/sp_02\nweapons=XM8_sp_s@Primary9\n","unknown weapon slot"));
    CHECK(Rejects("level=Levels/sp_02\nweapons=XM8_sp_s@Primary1,SPAS12_sp@Primary1\n","duplicate weapon slot"));
    CHECK(Rejects("level=Levels/sp_02\nweapons=[XM8_sp_s\n"));
    CHECK(Rejects("level=Levels/sp_02\nweapons=a,b,c,d,e,f,g,h,i\n"));
    CHECK(Rejects("level=Levels/sp_02\nweapons=\n"));
    CHECK(Rejects(With("select=SPAS12_sp\n"),"select is not in weapons"));
    CHECK(Rejects(With("ammo.SPAS12_sp=full\n"),"not in weapons"));
    CHECK(Rejects(With("ammo.XM8_sp_s=native/native\n")));
    CHECK(Rejects(With("ammo.XM8_sp_s=-1/30\n")));
    CHECK(Rejects(With("ammo.XM8_sp_s=100001/0\n")));
    CHECK(Rejects(With("ammo=some\n")));
    CHECK(Rejects(With("teleport=1,2\n")));
    CHECK(Rejects(With("teleport=1,2,3,400\n")));
    CHECK(Rejects(With("teleport=1e3,2,3\n")));
    CHECK(Rejects(With("teleport=nan,2,3\n")));
    CHECK(Rejects(With("fixed_dt=0.5\n")));
    CHECK(Rejects(With("fixed_dt=1e-2\n")));
    CHECK(Rejects(With("god=yes\n")));
    CHECK(Rejects(With("kit=14\n")));
    CHECK(Rejects(With("kit=03\n")));
    CHECK(Rejects(With("timeout=10\nready_timeout=20\n"),"ready_timeout exceeds timeout"));
    CHECK(Rejects(With("timeout=0\n")));
    CHECK(Rejects(With("route=registry\n")));
    CHECK(Rejects(With("route=manual\nspawn=Start\n"),"route=menu or override"));
    CHECK(Rejects(With("route=settings\n"))); // the command-line route is a dead end and not a value
    CHECK(Rejects(With("set.Client.Level=Levels/sp_03\n"),"Client.Level is set twice"));
    CHECK(Rejects(With("god=true\nset.server.isgodmode=false\n"),"set twice"));
    CHECK(Rejects(With("set.Client=1\n"),"invalid setting key"));
    CHECK(Rejects(With("set.Client.A.B=1\n"),"invalid setting key"));
    CHECK(Rejects(With("set.Client.Foo=-x\n")));
    CHECK(Rejects(With("set.Client.Foo=a\"b\n")));
    CHECK(Rejects(With("set.Client.Foo=a;b\n")));
    CHECK(Rejects(With("set.Client.Foo=x\nset.client.foo=y\n"),"duplicate key"));
    CHECK(Rejects(With("notes=caf\xC3\xA9\n"),"non-ASCII"));
    CHECK(Rejects(With("just text\n"),"expected key=value"));
    CHECK(Rejects(With("notes="+std::string(600,'a')+"\n"),"line too long"));
    {   // The error names the line.
        RangeSpec r;std::string e;CHECK(!ParseRange("FVR_BC2_RANGE 1\n# c\nlevel=Levels/sp_02\nbad=1\n",r,e));CHECK(e.rfind("line 4: ",0)==0);
    }
    {   // Passthrough limits.
        std::string body(Base);for(unsigned n=0;n<=RangeMaxSettings;++n)body+="set.Client.F"+std::to_string(n)+"=1\n";
        CHECK(Rejects(body,"too many"));
    }
    std::puts("Test range spec parsing, validation and Python parity passed.");return 0;
}
