#include "Test.h"
#include "Bc2Profile.h"
#include "Bc2WeaponMode.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
int wmain(int argc,wchar_t** argv){
    CHECK(argc==2);const std::filesystem::path path=argv[1];const auto n=std::filesystem::file_size(path);CHECK(n<512ull*1024*1024);
    std::vector<std::byte> bytes(std::size_t(n),std::byte{});std::ifstream file(path,std::ios::binary);CHECK(file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));
    const auto pe=fvr::engine::InspectPe(bytes);CHECK(pe.valid);const auto found=fvr::bc2::DiscoverGameplay(bytes,pe.image);CHECK(found);
    const auto& p=*found;std::cout<<"{\"context\":"<<p.contextObject<<",\"update\":"<<p.playerInputUpdate<<",\"gather\":"<<p.inputGather<<",\"router_vtable\":"<<p.inputRouterVtable<<",\"mutations_rejected\":";
    const auto offset=[&](unsigned rva){for(const auto& s:pe.image.sections)if(rva>=s.rva&&rva-s.rva<s.rawSize)return s.rawOffset+rva-s.rva;return 0u;};
    unsigned mutations=0;
    // Reject wrong argument cleanup, caller/cache relationship, getter and vtable.
    for(unsigned rva:{p.playerInputUpdate+0x1e9,p.inputGather+0x230,p.playerInputUpdate+0xe9,p.contextGetter+0x74,p.soldierGetter+2,p.inputRouterVtable+12}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};CHECK(!fvr::bc2::DiscoverGameplay(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    CHECK(fvr::bc2::DiscoverGameplay(bytes,pe.image));
    const auto binding=fvr::bc2::DiscoverInputBinding(bytes,pe.image);CHECK(binding);
    for(unsigned rva:{binding->buttonSetter+0x5b,binding->floatSetter+0x6c,binding->controlledGetter+2,binding->entryActions+7*24+16,binding->entryActions+16*24+16,binding->entryActions+29*24+16,binding->entryActions+38*24+16,binding->attachedPredicate+0x2e}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};CHECK(!fvr::bc2::DiscoverInputBinding(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    const auto aim=fvr::bc2::DiscoverAiming(bytes,pe.image);CHECK(aim);
    for(unsigned rva:{aim->aimGetter+2,aim->weaponGetter+0x26,aim->absoluteYawSetter+10,aim->angleCopy+0x28,aim->aimerYawSetter+0x32,aim->aimerPitchSetter+3}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};CHECK(!fvr::bc2::DiscoverAiming(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    const auto anchor=fvr::bc2::DiscoverViewAnchor(bytes,pe.image);CHECK(anchor);
    for(unsigned rva:{anchor->getter+2,anchor->setter+13,anchor->objectPrepare+0x92,anchor->projectionCaller-4}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};CHECK(!fvr::bc2::DiscoverViewAnchor(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    const auto pose=fvr::bc2::DiscoverFirstPersonPose(bytes,pe.image);CHECK(pose);
    for(unsigned rva:{pose->worldBuilder+0x202,pose->animationUpdate+0xf6,pose->animationUpdate+0x187,pose->rootSetter+13,pose->rootSetter+0x54}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};CHECK(!fvr::bc2::DiscoverFirstPersonPose(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    const auto body=fvr::bc2::DiscoverBodyPosition(bytes,pe.image);CHECK(body);
    for(unsigned rva:{body->getter+2,body->getter+0x14,body->fallback+5}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};CHECK(!fvr::bc2::DiscoverBodyPosition(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    const auto rig=fvr::bc2::DiscoverRig(bytes,pe.image);CHECK(rig);
    for(unsigned rva:{rig->animationGetter+12,rig->animationUpdate+0x35,rig->animationUpdate+0x2f1,rig->animationUpdate+0x81,rig->evaluate+0xd8,rig->postEvaluate+0xe7,rig->weaponWorld+6,rig->boneWorld+0x23,rig->worldThunk+0xa,rig->worldIndex+0x10,rig->skinSelect+2,rig->skinGetter+2,rig->paletteGetter+2,rig->skinThunk+5,rig->skinData+6}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};CHECK(!fvr::bc2::DiscoverRig(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    const auto consumer=fvr::bc2::DiscoverRigConsumer(bytes,pe.image);CHECK(consumer);
    for(unsigned rva:{consumer->prepare+0x120,consumer->getterA+4,consumer->getterB+0xf,consumer->pack+0x29,consumer->pack+0xbb,consumer->pack+0xbe,consumer->pack+0xcd}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};CHECK(!fvr::bc2::DiscoverRigConsumer(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    const auto fire=fvr::bc2::DiscoverFireOrigin(bytes,pe.image);CHECK(fire);
    for(unsigned rva:{fire->builder+0x156,fire->builder+0x152,fire->serverShoot+0x17,fire->serverShoot+0x383,fire->serverControlledGetter+2,fire->serverContextGetter+0x98,fire->serverManagerConstructor+0x2b,fire->serverPlayerCreate+0x4f,fire->serverPlayerCreate+0x59,fire->serverPlayerConstructor+0xf,fire->playerConstructor+0x56,fire->clientShoot+0xb9c,fire->clientCopyA-4,fire->clientCopyB-4,fire->matrixCopy+0x55,fire->compose+0xae,fire->clientCompose-4,fire->serverCompose-4,fire->serverShoot+0xc8e,fire->serverShoot+0x6d4,fire->clientShoot+0x4e5,fire->serverShoot+0x6f8,fire->clientShoot+0x50f,fire->spreadSeed+2,fire->spreadSeed+4,fire->randomSeed+5}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};CHECK(!fvr::bc2::DiscoverFireOrigin(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    const auto mode=fvr::bc2::DiscoverWeaponMode(bytes,pe.image);CHECK(mode);
    for(unsigned rva:{mode->selector+0x4f,mode->selector+0x100,mode->selector+0x1ae,mode->update+0x4d,
        mode->update+0x83,mode->edgeReader+0xf,mode->edgeReader+0x1f,mode->booleanReader+0x55,mode->scalarReader+0x3a,
        binding->entryActions+33*24+16,binding->entryActions+36*24+16}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};
        CHECK(!fvr::bc2::DiscoverWeaponMode(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    const auto effects=fvr::bc2::DiscoverWeaponEffects(bytes,pe.image);CHECK(effects);
    CHECK(effects->spawnReturn==effects->effectsUpdate+0x1c0);
    CHECK(effects->composeReturn==effects->effectsUpdate+0x1ae);
    for(unsigned rva:{rig->animationUpdate+0x131,rig->animationUpdate+0x141,effects->firingEffectsGetter+2,effects->effectsUpdate+0x7b,
        effects->effectsUpdate+0x1af,effects->effectsUpdate+0x1bc,effects->effectsUpdate+0x1d0,effects->effectsUpdate+0x1dc,
        effects->effectsUpdate+0x190,effects->effectsUpdate+0x1aa,effects->compose+0x38f,effects->compose+0x3df,
        effects->compose+0x3fc,effects->compose+0x433,effects->compose+0x4b2,
        effects->spawn+0x3a,effects->spawn+0x61,effects->create+0x1f,effects->create+0x2f,effects->create+0x41,
        effects->overheat+0x3f,effects->overheat+0xbf,effects->overheat+0x152,effects->setTransform+0x0a,effects->setTransform+0x2a,
        effects->setTransform+0x23,effects->setTransform+0x31,effects->setTransform+0x48,effects->matrixCopy+2,effects->lookup+0x10}){
        const auto at=offset(rva);CHECK(at);const auto saved=bytes[at];bytes[at]^=std::byte{1};
        CHECK(!fvr::bc2::DiscoverWeaponEffects(bytes,pe.image));bytes[at]=saved;++mutations;
    }
    CHECK(fvr::bc2::DiscoverWeaponEffects(bytes,pe.image));
    std::cout<<mutations<<",\"weapon_effects\":{\"effects_update\":"<<effects->effectsUpdate<<",\"spawn\":"<<effects->spawn<<",\"spawn_return\":"<<effects->spawnReturn
             <<",\"create\":"<<effects->create<<",\"set_transform\":"<<effects->setTransform<<",\"overheat\":"<<effects->overheat<<"}"
             <<",\"native_writes_enabled\":false}\n";return 0;
}
