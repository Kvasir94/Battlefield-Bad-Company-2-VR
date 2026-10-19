#include "fvr/runtime/TestRange.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <istream>
#include <iterator>
namespace fvr::runtime {
namespace {
bool Alnum(char c)noexcept{return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}
bool Alpha(char c)noexcept{return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
char Lower(char c)noexcept{return c>='A'&&c<='Z'?char(c-'A'+'a'):c;}
bool SameFold(std::string_view a,std::string_view b)noexcept{
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){return Lower(x)==Lower(y);});
}
std::string_view Trim(std::string_view s)noexcept{
    while(!s.empty()&&(s.front()==' '||s.front()=='\t'))s.remove_prefix(1);
    while(!s.empty()&&(s.back()==' '||s.back()=='\t'||s.back()=='\r'))s.remove_suffix(1);
    return s;
}
bool Fail(std::string& error,std::string message){error=std::move(message);return false;}
bool Bool(std::string_view v,std::optional<bool>& out){
    if(v=="true"){out=true;return true;}
    if(v=="false"){out=false;return true;}
    return false;
}
bool Bool(std::string_view v,bool& out){std::optional<bool> b;if(!Bool(v,b))return false;out=*b;return true;}
bool Unsigned(std::string_view v,unsigned lo,unsigned hi,unsigned& out){
    if(v.empty()||v.size()>10||(v.size()>1&&v.front()=='0'))return false;
    for(char c:v)if(c<'0'||c>'9')return false;
    unsigned value=0;const auto r=std::from_chars(v.data(),v.data()+v.size(),value);
    if(r.ec!=std::errc{}||r.ptr!=v.data()+v.size()||value<lo||value>hi)return false;
    out=value;return true;
}
// Classic-locale decimal: -?digits(.digits)? ; no exponent, no inf/nan, <=24 chars.
bool Decimal(std::string_view v,float lo,float hi,float& out){
    if(v.empty()||v.size()>24)return false;
    std::size_t i=v.front()=='-'?1:0,digits=0,dot=0;
    for(;i<v.size();++i){if(v[i]=='.'){if(++dot>1||!digits)return false;}else if(v[i]>='0'&&v[i]<='9')++digits;else return false;}
    if(!digits||v.back()=='.')return false;
    double value=0;const auto r=std::from_chars(v.data(),v.data()+v.size(),value);
    if(r.ec!=std::errc{}||r.ptr!=v.data()+v.size()||!std::isfinite(value)||value<lo||value>hi)return false;
    out=float(value);return true;
}
std::vector<std::string_view> Split(std::string_view v,char sep){
    std::vector<std::string_view> out;
    for(;;){const auto at=v.find(sep);out.push_back(Trim(v.substr(0,at)));if(at==std::string_view::npos)break;v.remove_prefix(at+1);}
    return out;
}
bool Rounds(std::string_view v,int& out){unsigned n=0;if(!Unsigned(v,0,RangeMaxRounds,n))return false;out=int(n);return true;}
const char* Text(bool b)noexcept{return b?"true":"false";}
}
bool ValidRangeAsset(std::string_view s)noexcept{
    if(s.empty()||s.size()>64||!Alnum(s.front()))return false;
    return std::all_of(s.begin(),s.end(),[](char c){return Alnum(c)||c=='_'||c=='-';});
}
bool ValidRangeLevel(std::string_view s)noexcept{
    if(s.size()<8||!SameFold(s.substr(0,7),"Levels/"))return false;
    const auto id=s.substr(7);
    return std::any_of(std::begin(RangeLevelIds),std::end(RangeLevelIds),[&](std::string_view known){return SameFold(id,known);});
}
bool ValidRangeIdentifier(std::string_view s)noexcept{
    if(s.empty()||s.size()>64||!Alnum(s.front()))return false;
    return std::all_of(s.begin(),s.end(),[](char c){return Alnum(c)||c=='_'||c=='-'||c=='.';});
}
bool ValidRangeSettingKey(std::string_view s)noexcept{
    const auto dot=s.find('.');
    if(dot==std::string_view::npos||s.find('.',dot+1)!=std::string_view::npos||s.size()>96)return false;
    for(const auto part:{s.substr(0,dot),s.substr(dot+1)}){
        if(part.empty()||!Alpha(part.front()))return false;
        if(!std::all_of(part.begin(),part.end(),[](char c){return Alnum(c)||c=='_';}))return false;
    }
    return true;
}
bool ValidRangeSettingValue(std::string_view s)noexcept{
    // One argv element with no quoting: printable ASCII without space, quotes,
    // backslash or shell metacharacters, and never a leading '-' (a new switch).
    if(s.empty()||s.size()>128||s.front()=='-')return false;
    return std::all_of(s.begin(),s.end(),[](char c){
        return c>0x20&&c<0x7f&&c!='"'&&c!='\''&&c!='\\'&&c!='`'&&c!='$'&&c!='&'&&c!='|'&&c!=';'&&c!='<'&&c!='>'&&c!='%'&&c!='^';});
}
bool ApplyRangeKey(RangeSpec& r,std::string_view key,std::string_view value,std::vector<std::string>& seen,std::string& error){
    const std::string k(key);
    if(key.empty())return Fail(error,"empty key");
    if(std::find(seen.begin(),seen.end(),k)!=seen.end())return Fail(error,"duplicate key "+k);
    const auto bad=[&](){return Fail(error,"invalid value for "+k);};
    if(key=="name"){if(!ValidRangeIdentifier(value))return bad();r.name=value;}
    else if(key=="notes"){
        if(value.size()>256||!std::all_of(value.begin(),value.end(),[](char c){return c>=0x20&&c<0x7f;}))return bad();
        r.notes=value;
    }
    else if(key=="level"){if(!ValidRangeLevel(value))return bad();r.level=value;}
    else if(key=="route"){
        if(value=="menu")r.route=RangeLevelRoute::Menu;
        else if(value=="override")r.route=RangeLevelRoute::Override;
        else if(value=="manual")r.route=RangeLevelRoute::Manual;else return bad();
    }
    else if(key=="spawn"){if(!ValidRangeIdentifier(value))return bad();r.spawn=value;}
    else if(key=="savepoint"){if(!ValidRangeIdentifier(value))return bad();r.savepoint=value;}
    else if(key=="weapons"){
        auto list=value;
        if(!list.empty()&&list.front()=='['){if(list.size()<2||list.back()!=']')return bad();list=list.substr(1,list.size()-2);}
        std::vector<std::string> weapons;std::vector<int> slots;
        for(auto item:Split(list,',')){
            int slot=-1;
            if(const auto at=item.find('@');at!=std::string_view::npos){
                const auto name=item.substr(at+1);item=item.substr(0,at);
                for(int n=0;n<int(std::size(RangeSlotNames));++n)if(name==RangeSlotNames[n])slot=n;
                if(slot<0)return Fail(error,"unknown weapon slot "+std::string(name));
                for(int s:slots)if(s==slot)return Fail(error,"duplicate weapon slot "+std::string(name));
            }
            if(!ValidRangeAsset(item))return bad();
            for(const auto& w:weapons)if(SameFold(w,item))return Fail(error,"duplicate weapon "+std::string(item));
            weapons.emplace_back(item);slots.push_back(slot);
        }
        if(weapons.empty()||weapons.size()>RangeMaxWeapons)return bad();
        r.weapons=std::move(weapons);r.weaponSlots=std::move(slots);
    }
    else if(key=="select"){if(!ValidRangeAsset(value))return bad();r.select=value;}
    else if(key=="clear_inventory"){if(!Bool(value,r.clearInventory))return bad();}
    else if(key=="skip_cinematics"){if(!Bool(value,r.skipCinematics))return bad();}
    else if(key=="ammo"){if(value=="native")r.ammo=RangeAmmoMode::Native;else if(value=="full")r.ammo=RangeAmmoMode::Full;else return bad();}
    else if(key.starts_with("ammo.")){
        RangeAmmoOverride o;o.asset=key.substr(5);
        if(!ValidRangeAsset(o.asset))return Fail(error,"invalid asset in "+k);
        for(const auto& e:r.ammoOverrides)if(SameFold(e.asset,o.asset))return Fail(error,"duplicate key "+k);
        if(value=="full")o.full=true;
        else{
            const auto parts=Split(value,'/');
            if(parts.size()!=2)return bad();
            if(parts[0]!="native"&&!Rounds(parts[0],o.loaded))return bad();
            if(parts[1]!="native"&&!Rounds(parts[1],o.reserve))return bad();
            if(o.loaded<0&&o.reserve<0)return bad();
        }
        if(r.ammoOverrides.size()>=RangeMaxWeapons)return bad();
        r.ammoOverrides.push_back(std::move(o));
    }
    else if(key=="teleport"){
        const auto parts=Split(value,',');
        if(parts.size()!=3&&parts.size()!=4)return bad();
        RangeTeleport t;float* f[]{&t.x,&t.y,&t.z,&t.yaw};
        for(std::size_t n=0;n<parts.size();++n)if(!Decimal(parts[n],n==3?-360.f:-100000.f,n==3?360.f:100000.f,*f[n]))return bad();
        t.hasYaw=parts.size()==4;t.text=value;r.teleport=std::move(t);
    }
    else if(key=="menu"){if(!Bool(value,r.menu))return bad();}
    else if(key=="skip_intro"){if(!Bool(value,r.skipIntro))return bad();}
    else if(key=="fast_startup"){if(!Bool(value,r.fastStartup))return bad();}
    else if(key=="cutscenes"){if(!Bool(value,r.cutscenes))return bad();}
    else if(key=="instant_spawn"){if(!Bool(value,r.instantSpawn))return bad();}
    else if(key=="unlimited_ammo"){if(!Bool(value,r.unlimitedAmmo))return bad();}
    else if(key=="unlimited_mags"){if(!Bool(value,r.unlimitedMags))return bad();}
    else if(key=="ai"){if(!Bool(value,r.ai))return bad();}
    else if(key=="god"){if(!Bool(value,r.god))return bad();}
    else if(key=="checkpoints"){if(!Bool(value,r.checkpoints))return bad();}
    else if(key=="unlocks"){if(!Bool(value,r.unlocks))return bad();}
    else if(key=="scripted_input"){if(!Bool(value,r.scriptedInput))return bad();}
    else if(key=="kit"){unsigned n=0;if(!Unsigned(value,0,13,n))return bad();r.kit=n;}
    else if(key=="team"){unsigned n=0;if(!Unsigned(value,0,15,n))return bad();r.team=n;}
    else if(key=="fixed_dt"){float dt=0;if(!Decimal(value,0.001f,0.1f,dt))return bad();r.fixedDt=value;}
    else if(key=="timeout"){if(!Unsigned(value,1,RangeMaxTimeoutS,r.timeoutS))return bad();}
    else if(key=="ready_timeout"){if(!Unsigned(value,1,RangeMaxTimeoutS,r.readyTimeoutS))return bad();}
    else if(key.starts_with("set.")){
        const auto setting=key.substr(4);
        if(!ValidRangeSettingKey(setting))return Fail(error,"invalid setting key "+k);
        if(!ValidRangeSettingValue(value))return bad();
        for(const auto& s:r.settings)if(SameFold(s.key,setting))return Fail(error,"duplicate key "+k);
        if(r.settings.size()>=RangeMaxSettings)return Fail(error,"too many set.* keys");
        r.settings.push_back({std::string(setting),std::string(value)});
    }
    else return Fail(error,"unknown key "+k);
    seen.push_back(k);return true;
}
bool ValidateRange(const RangeSpec& r,std::string& error){
    if(r.level.empty())return Fail(error,"missing level");
    if(r.weapons.empty())return Fail(error,"missing weapons");
    const auto listed=[&](const std::string& asset){
        return std::any_of(r.weapons.begin(),r.weapons.end(),[&](const std::string& w){return w==asset;});};
    if(!r.select.empty()&&!listed(r.select))return Fail(error,"select is not in weapons");
    for(const auto& o:r.ammoOverrides)if(!listed(o.asset))return Fail(error,"ammo."+o.asset+" is not in weapons");
    if(r.readyTimeoutS>r.timeoutS)return Fail(error,"ready_timeout exceeds timeout");
    if(r.route==RangeLevelRoute::Manual&&(!r.spawn.empty()||!r.savepoint.empty()))
        return Fail(error,"spawn/savepoint are level-load settings and need route=menu or override");
    const auto pairs=RangeSettings(r);
    for(std::size_t i=0;i<pairs.size();++i)for(std::size_t j=i+1;j<pairs.size();++j)
        if(SameFold(pairs[i].key,pairs[j].key))return Fail(error,"setting "+pairs[i].key+" is set twice");
    return true;
}
bool ParseRange(std::string_view text,RangeSpec& out,std::string& error){
    RangeSpec r;std::vector<std::string> seen;bool header=false;unsigned line=0;
    if(text.starts_with("\xEF\xBB\xBF"))return Fail(error,"UTF-8 BOM is not allowed");
    while(!text.empty()||line==0){
        const auto end=text.find('\n');auto raw=text.substr(0,end);
        text=end==std::string_view::npos?std::string_view{}:text.substr(end+1);++line;
        const auto where="line "+std::to_string(line)+": ";
        if(raw.size()>RangeMaxLineBytes)return Fail(error,where+"line too long");
        if(std::any_of(raw.begin(),raw.end(),[](char c){return (c<0x20&&c!='\t'&&c!='\r')||c==0x7f||(unsigned char)c>=0x80;}))
            return Fail(error,where+"non-ASCII or control character");
        const auto s=Trim(raw);
        if(s.empty()||s.front()=='#'||s.front()==';'){if(text.empty())break;continue;}
        if(!header){
            if(s!="FVR_BC2_RANGE 1")return Fail(error,where+"expected header FVR_BC2_RANGE 1");
            header=true;if(text.empty())break;continue;
        }
        const auto eq=s.find('=');
        if(eq==std::string_view::npos)return Fail(error,where+"expected key=value");
        std::string why;
        if(!ApplyRangeKey(r,Trim(s.substr(0,eq)),Trim(s.substr(eq+1)),seen,why))return Fail(error,where+why);
        if(text.empty())break;
    }
    if(!header)return Fail(error,"missing header FVR_BC2_RANGE 1");
    std::string why;if(!ValidateRange(r,why))return Fail(error,why);
    out=std::move(r);return true;
}
bool ParseRange(std::istream& in,RangeSpec& out,std::string& error){
    std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(in.bad())return Fail(error,"read failed");
    if(text.size()>64*1024)return Fail(error,"range file too large");
    return ParseRange(text,out,error);
}
std::vector<RangeSetting> RangeSettings(const RangeSpec& r){
    std::vector<RangeSetting> a;
    const auto put=[&](const char* key,std::string value){a.push_back({key,std::move(value)});};
    if(!r.level.empty())put("Client.Level",r.level); // newSP loads Client.Level
    if(!r.spawn.empty())put("Client.SpawnPoint",r.spawn);
    if(!r.savepoint.empty()){put("Server.SavePoint",r.savepoint);put("Server.LoadSavePoint","true");}
    if(r.menu)put("Client.LoadMenu",Text(*r.menu));
    if(r.skipIntro)put("Client.DisableMovies",Text(*r.skipIntro));
    if(r.fastStartup)put("Client.IsFastStartupEnabled",Text(*r.fastStartup));
    if(r.cutscenes)put("Server.DisableCutscenes",Text(!*r.cutscenes));
    if(r.ai)put("Server.IsAiEnabled",Text(*r.ai));
    if(r.god)put("Server.IsGodMode",Text(*r.god));
    if(r.checkpoints)put("Server.DisableCheckpoints",Text(!*r.checkpoints));
    if(r.unlocks){put("Server.UnrestrictedUnlocks",Text(*r.unlocks));put("Client.UnrestrictedUnlocks",Text(*r.unlocks));}
    if(r.instantSpawn)put("Server.InstantSpawn",Text(*r.instantSpawn));
    if(r.unlimitedAmmo)put("Server.HasUnlimitedAmmo",Text(*r.unlimitedAmmo));
    if(r.unlimitedMags)put("Server.HasUnlimitedMags",Text(*r.unlimitedMags));
    if(r.kit)put("Client.Kit",std::to_string(*r.kit));
    if(r.team)put("Client.Team",std::to_string(*r.team));
    if(!r.fixedDt.empty())put("Game.ForceDeltaTime",r.fixedDt);
    if(r.scriptedInput)put("Client.UseScriptedInput",Text(*r.scriptedInput));
    for(const auto& s:r.settings)a.push_back(s);
    return a;
}
std::string FormatRange(const RangeSpec& r){
    std::string o="FVR_BC2_RANGE 1\n";
    const auto put=[&](std::string_view key,std::string_view value){o.append(key);o+='=';o.append(value);o+='\n';};
    const auto opt=[&](std::string_view key,const std::optional<bool>& v){if(v)put(key,Text(*v));};
    if(!r.name.empty())put("name",r.name);
    if(!r.notes.empty())put("notes",r.notes);
    put("level",r.level);
    put("route",r.route==RangeLevelRoute::Manual?"manual":r.route==RangeLevelRoute::Override?"override":"menu");
    if(!r.spawn.empty())put("spawn",r.spawn);
    if(!r.savepoint.empty())put("savepoint",r.savepoint);
    std::string list;
    for(std::size_t n=0;n<r.weapons.size();++n){
        if(n){list+=',';}
        list+=r.weapons[n];
        const int slot=n<r.weaponSlots.size()?r.weaponSlots[n]:-1;
        if(slot>=0&&slot<int(std::size(RangeSlotNames))){list+='@';list.append(RangeSlotNames[slot]);}
    }
    put("weapons",list);
    if(!r.select.empty())put("select",r.select);
    put("clear_inventory",Text(r.clearInventory));put("skip_cinematics",Text(r.skipCinematics));
    put("ammo",r.ammo==RangeAmmoMode::Full?"full":"native");
    for(const auto& a:r.ammoOverrides){
        const auto n=[](int v){return v<0?std::string("native"):std::to_string(v);};
        put("ammo."+a.asset,a.full?std::string("full"):n(a.loaded)+"/"+n(a.reserve));
    }
    if(r.teleport)put("teleport",r.teleport->text);
    opt("menu",r.menu);opt("skip_intro",r.skipIntro);opt("fast_startup",r.fastStartup);opt("cutscenes",r.cutscenes);opt("ai",r.ai);opt("god",r.god);
    opt("checkpoints",r.checkpoints);opt("unlocks",r.unlocks);opt("instant_spawn",r.instantSpawn);
    opt("unlimited_ammo",r.unlimitedAmmo);opt("unlimited_mags",r.unlimitedMags);
    if(r.kit)put("kit",std::to_string(*r.kit));
    if(r.team)put("team",std::to_string(*r.team));
    if(!r.fixedDt.empty())put("fixed_dt",r.fixedDt);
    opt("scripted_input",r.scriptedInput);
    put("timeout",std::to_string(r.timeoutS));put("ready_timeout",std::to_string(r.readyTimeoutS));
    for(const auto& s:r.settings)put("set."+s.key,s.value);
    return o;
}
}
