> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 runtime entity spawning and dropped-magazine routes (build 795745, x86, base 0x400000)

> Cloud static disassembly of build 795745 ([`tools/re/bc2re.py`](../../tools/re/bc2re.py)), 2026-10-08.
> In this report **SV** means read in the disassembly; it is the **DIS** level in
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md). Nothing here has run in the game unless marked LO. Script names refer to throwaway analysis scripts that are not in the repo.

Static analysis only; nothing here is runtime-validated. Labels: **SV** = read in disassembly,
**DUMP** = from bc2_types.json, **INF** = inference.
Signatures were checked for uniqueness over .text+ctr, with `??` for rel32 or absolute operands
(scripts: `spawn_sig.py`, `spawn_creators.py`; full creator table: `spawn_creators.json`).

## 1. Native runtime entity-creation API

### Model: per-EntityData-type "creator" objects in two registries (SV)
- Each creator is a 4-byte object holding only a vtable. Registration:
  `push 4; call 0x420db0 (alloc); mov [eax],creatorVt; push eax; mov ecx,REG; call REGFN`.
- **Server registry**: the static map at 0x157041c is filled by the lazy getter
  **0x71c7f0** `Registry* ServerCreators()`, which inserts 148 creators through 0x71c790.
  Sig: `F6 05 ?? ?? ?? ?? 01 75 32 83 0D ?? ?? ?? ?? 01 68 ?? ?? ?? ?? C7 05 ?? ?? ?? ?? 00 00 00 00`.
  All 148 resolved; 16 of them are a templated creator for `*OrderEntityData` whose TypeInfo is stored at creator+4.
- **Client registry**: the static at 0x1572768 is filled by **0x8b6b10** `ClientCreators()`, which inserts 100 creators through 0x8b6a60.
  Sig: `55 8B EC 83 E4 F8 83 EC 08 53 33 DB F6 05 ?? ?? ?? ?? 01 56 75 38`.
- Server creator vtable: [0] dtor, [1] `TypeInfo* dataType()`, [2] `createEntity(info)` (shared 0x711740),
  [3] `size(data)`, [4] `Entity* placementCreate(info, mem)`.
  Slot 4 is `mem = 0x610b40(size, mem)` (placement new) followed by a tail-jump to `Ctor(this=mem, info, info->data)`.
  Example: the ServerPickupEntity creator 0x6fe2e0 (vt 0x1427cac) has size 0x180 and ctor 0x6ad610.
- Client creator vtable: [0] dtor, [1] dataType, [2] createEntity (shared **0x8a07f0**), [3] size, [4]/[5] return false,
  [6] placementCreate(info, mem). The client placementCreate passes `info+0x2c` as the data pointer.
- **0x711740** server `Creator::createEntity(EntityCreationInfo*)`: thiscall, ret 4.
  It aligns `size = vf3(data)` to 16, adds component storage (0x7116f0 counts components recursively),
  and allocates with 0x420c90(size, 16, 0) unless the caller supplied a block.
  It then calls `vf4(info, mem)` and frees the block if the result is NULL.
  Sig: `55 8B EC 83 E4 F0 83 EC 54 8B 01 8B 40 0C 53 56 8B 75 08 8B 56 24` (SV).
  The client twin 0x8a07f0 does the same with data at info+0x2c.
  Sig: `55 8B EC 83 E4 F0 83 EC 54 8B 01 8B 50 0C 53 56 8B 75 08 57 56 89 4C 24 20` (SV).

### Public factory entry points (SV)
| Address | Role | ABI | Signature |
|---|---|---|---|
| 0x6f67d0 | `ServerEntityCreationInfo::ctor(EntityData*)` | thiscall, ret 4; 0x34-byte struct | `56 57 8B F1 E8 ?? ?? ?? ?? 8B 78 3C E8 ?? ?? ?? ?? 8B 40 04 8B 4C 24 0C` |
| 0x71dd00 | `Entity* ServerEntityFactory::create(ServerEntityCreationInfo*)` | cdecl, 1 arg | `83 EC 40 55 8B 6C 24 48 80 7D 0C 00 56 57 8B 7D 24 74 09 8A 45 30` |
| 0x716bf0 | `ServerCreators()->create(info)`: lookup by `data->getType()`, then `creator->vf2(info)` | thiscall, ret 4 | `53 55 8B 6C 24 0C 56 8B F1 8B 4D 24 8B 01 8B 50 08 57 FF D2 8B 7E 04` |
| 0x648820 | `ServerEntity* createFromData(EntityData*)` = ctor + create | cdecl | 9 byte-identical copies exist (0x5437d0, 0x54c930, ...), so it has no unique sig. Use the death-pickup call site below instead. |
| 0x874af0 | `ClientEntityCreationInfo::ctor(EntityData*)` | thiscall, ret 4; 0x40-byte struct | `56 57 8B F1 E8 ?? ?? ?? ?? 8B 78 44 E8 ?? ?? ?? ?? 8B 40 04 8B 4C 24 0C` |
| 0x8b8360 | `Entity* ClientEntityFactory::create(ClientEntityCreationInfo*)` | cdecl, 1 arg | `83 EC 58 53 8B 5C 24 60 80 7B 0C 00 56 57 74 09 8A 43 3D 88 44 24 68` |
| 0x8ba070 | `ClientEntity* createClient(EntityData*, bool flag)`; flag goes to info+0x1c, and info+0x34=1 | ret 8, ecx unused | `83 EC 40 8B 44 24 44 50 8D 4C 24 04 E8 ?? ?? ?? ?? 33 C9 38 4C 24 48` |

The two info ctors (SV):
- Server info, from `ctx = 0x547bf0()` (static 0x1566b18):
  `+0 = ctx->+4` (world/owner), `+4 = ctx->+0x3c`, `+8 mem = 0`, `+0xc ownsBlock = 0`, `+0x1c createComponents = 1`,
  `+0x24 = data`, `+0x28 = 0`, `+0x30 = 0`.
- Client info, from `ctx = 0x7ff060()` (static 0x15713d8):
  `+0 = ctx->+4`, `+4 = ctx->+0x44`, `+0x20 = -1`, `+0x24 = 1`, `+0x2c = data`, `+0x30 = 0`.
  Sig for 0x7ff060: `B8 01 00 00 00 84 05 ?? ?? ?? ?? 75 66 09 05 ?? ?? ?? ?? 50 B9 ?? ?? ?? ??`.
- Both factories recurse into `data->Components` (GameObjectData+0x18) and attach each child with 0xff9a10.
- After creation, if `info+0x28` (server) or `info+0x30` (client) is 0 and the result is a Server/ClientEntity whose
  `vf[0x8c/4]()` returns false, the factory calls Entity::destroy and the caller gets NULL. (On the server, a non-GameEntity,
  non-Component result goes to 0x634a30(info->+0, e) instead.)

### Transform: not in the info struct; set afterwards with vf[0x94/4] (SV)
`entity->vf[0x94/4](const LinearTransform* m)` sets the transform; the matrix is 0x40 bytes and its translation is at +0x30.
- ServerPickupEntity: vt 0x1424358, slot 0x94 = 0x6a85b0. Sig: `55 8B EC 83 E4 F0 83 EC 78 56 57 8B 7D 08 57 8B F1 E8 ?? ?? ?? ??`.
- ClientDynamicModelEntity: slot 0x94 = 0x8cca70, which forwards to `physicsBody(+0x5c)->vf0x48/4`.
  Sig: `56 8B F1 8B 4E 5C 8B 01 8B 50 48 57 8B 7C 24 0C 57 FF D2 6A 01 6A 01`.

### Real use 1: soldier death drop (server only, SV)
The call site at 0x6c3a62 (sig `50 E8 ?? ?? ?? ?? 8B F8 83 C4 04 3B FB 0F 84 86 00 00 00 56 8B CF`, unique):
```c
PickupEntityData* d = soldier->data(+0xc)->DeathPickup /*SoldierEntityData+0x1ec, DUMP*/;
ServerPickupEntity* p = createFromData(d);          // 0x648820 -> 0x6f67d0 + 0x71dd00
p->fillFromSoldier(soldier);                        // 0x6c2ce0
LinearTransform m = soldierTransform(soldier+0xc0); // 0x6219d0
m.trans += offset_vec_at_0x1507f10;
p->vf0x94/4;                                  // setTransform
soldier+0x3bc <- weak handle(p);                    // 0x6ae760 / 0x69fb40
```
This is server-authoritative. The pickup replicates to clients and is interactable. Its lifetime comes from `PickupEntityData.TimeToLive` (+0x84, DUMP).

### Real use 2: client "SpawnedBanger" debris (client only, SV)
**0x8e2890** (thiscall, ret 0xc; sig `55 8B EC 83 E4 F0 81 EC 04 01 00 00 53 56 57 8B F9 8B 4F 04 8B 49 2C`)
creates a client physics debris entity when a destructible part changes health state:
```c
DynamicPhysicsEntityData* d = healthState->SpawnedBanger;   // HealthStateData+0x40 (DUMP)
if (!d || !isKindOf(d->getType(), DynamicModelEntityData /*TI 0x1c06ca8*/)) return;
ClientEntityCreationInfo ci; ClientEntityCreationInfo_ctor(&ci, d);    // 0x874af0
ClientDynamicModelEntity* e = ClientEntityFactory_create(&ci);         // 0x8b8360
body = 0x611090(e->physIface(+0x2c)->vf1());   // tail call to obj->vf[0xf4/4]: rigid-body wrapper (INF)
LinearTransform m = partWorldTransform(...);   // 0x62e610 / 0x622aa0 / 0x611e90
e->vf0x94/4;                             // setTransform
vel = random(...);                             // 0x4e45f0 = random float in a range
0xaa7c10(body, &vel);                          // thiscall ret 4; body->+0x8c hkpEntity vf0x40/4: setLinearVelocity (INF)
```
- Sig for 0xaa7c10, matching at fn+0x10: `8B B1 8C 00 00 00 0F 57 C0 0F C6 C1 ED 0F C6 C8 64 8B CE 0F 29 4C 24 10 E8 ?? ?? ?? ?? 8B 96 D0 00 00 00 8B 52 40`.
- Its sibling 0xaa7c60 uses vf 0x44 (INF: angular velocity).
- 0x8d3de0 (sig `8B 44 24 04 83 EC 40 56 8B 70 38 85 F6 74 19 8B 16 8B 42 08 8B CE`) is the same pattern:
  it creates a DynamicModelEntity from `arg->+0x38` and returns `e+0x3c`.
  It sits at slot 21 of interface vtable 0x1438de4, an entity subobject at +0x64 (INF: a destruction callback).

### Destruction (SV)
- **0x62d810** `Entity::destroy()`: thiscall, no args.
  Sig: `56 8B F1 F6 46 27 01 75 18 8B 06 8B 90 C0 00 00 00 56 FF D2 8B C8`.
  It returns early if byte +0x27 bit 0 is set. Otherwise it gets `mgr = this->vf[0xc0/4]()` and calls
  **0x62a960** `mgr->queueDelete(this)` (sig `51 53 55 8B 6C 24 10 8A 45 26 C0 E8 03 A8 01 56 57 8B D9 74 0F 8B 55 00`).
  Finally it sets the flag with `word +0x26 |= 0x100`.
- `vf 0xc0` returns the entity manager: server `*(0x156f2d4)` (0x68f6f0), client `*(0x15727b0)` (0x877ad0).
- The factory itself calls 0x62d810 on failed initialisation.
- INF: a ClientDynamicModelEntity has no timer of its own and lives until destroy() is called or the level tears down.

## 2. Client-only mesh/physics routes
**Client-only creators (SV).** These data types have a creator in the client registry and none in the server registry (all 148 server entries resolved):
DynamicModelEntityData, RigidMeshEntityData, CompositeMeshEntityData, AnimatedMeshEntityData, EffectEntityData,
EmitterEntityData, EffectComponentData, SoundEntityData, SoundAreaEntityData, lights, WeatherSystemEntityData,
StaticEnvmapEntityData.

| Data type (DUMP) | Client entity | Physics | Key fields |
|---|---|---|---|
| **DynamicModelEntityData** (0xb0, TI 0x1c06ca8; parent DynamicPhysicsEntityData → GamePhysicsEntityData) | ClientDynamicModelEntity (TI 0x1bf5648 SV, size 0x68, ctor **0x8ea050**, vt 0x1438548, creator vt 0x1435744 / placement 0x88d850) | Yes. The ctor calls `info->+0->+0x94` (physics world) `->vf2(data->PhysicsData, 3, 0x200000)`; the body is kept at +0x5c (SV) | GamePhysicsEntityData: Material +0x80, **PhysicsData +0x8c**. DynamicPhysicsEntityData: TrackedByAI +0x90. DynamicModelEntityData: MeshShaderSet +0xa0, **Mesh (MeshAsset) +0xa4**, Part +0xa8. The ctor reads +0x8c/+0xa0/+0xa4 (SV). |
| PhysicsEntityData (0x40) | — | — | InertiaModifier +0x10, Restitution +0x20, Friction +0x24, Proximity +0x28, **Asset (HavokAsset) +0x2c**, FloatPhysics +0x30, Mass +0x34 |
| RigidMeshEntityData (0x80) | client only | none (render only, INF) | MeshShaderSet +0x70, Mesh (RigidMeshAsset) +0x78 |
| RigidModelEntityData / StaticModelEntityData | created on both sides (networked or level-static) | yes | RigidModel: Mesh +0x94. StaticModel: Mesh +0x13c, PhysicsData +0x8c |
| EmitterEntityData → EmitterTemplateData | particle system | per particle | EmitterTemplateData has Mesh +0x98, Physics HavokAsset +0x9c, PhysicsData +0xdc, Lifetime +0xc8, MaxCount +0xb0, EmittableType +0x90. This is the shell-casing/debris route, but it is time-limited and pooled (INF). |
| HealthStateData | — | — | LoosePartPhysics +0x10 → FakePhysicsData (scripted, non-Havok trajectories), SpawnedBanger +0x40 |

- Shell ejection is not an entity in the dump (no shell or casing types or strings). INF: it is an EffectAsset/emitter with mesh particles,
  and it is unsuitable for persistence.
- Persistent client-only route: **create a ClientDynamicModelEntity with ClientEntityFactory (0x874af0 + 0x8b8360),
  then setTransform (vf 0x94), then set velocity (0xaa7c10)**. This is exactly what the engine does for SpawnedBanger debris.

## 3. Is the magazine a separate asset? No (DUMP + repo)
- The data chain is: SoldierWeaponData +0x88 WeaponStates[] → WeaponStateData (stride 0xc8):
  +0x80 Meshes1p[], +0x94 MeshZoom1p, +0x98 Meshes3p[] (all SkinnedMeshAsset, i.e. the whole weapon),
  +0x4c HiddenBones1p[], +0x70 HiddenBones3p[], +0x5c BoneFakePhysics[].
  None of SoldierWeaponData, WeaponFiringData, WeaponEffectsData, PickupSettingsData or FireEffectData has a magazine
  MeshAsset field (DUMP).
- The only magazine-related data are numbers (MagazineCapacity, NumberOfMagazines, AutoReplenishMagazine strings).
- Pickups only offer `PickupEntityData.UseWeaponMesh` (+0x96), which shows the full weapon.
- The repo agrees. `config/body-ammo-assets.json` and `tools/bc2_authored_magazine_geometry.py` treat the magazine as a
  bone-owned part (for example XM8 `jntWpn_6`, SPAS `jntWpn_7`) inside the weapon's skinned mesh. They extract the
  single-bone triangles from the user's installed archives. `Bc2BodyAmmoRenderer` / `D3D11RigidPropRenderer` already draw
  that geometry as a rigid prop.

## 4. Recommendation
**Use a mod-rendered persistent prop (preferred).**
- Reuse BodyAmmoGeometry / RigidPropRenderer with the existing per-weapon magazine part geometry, and give it a pose owned by the mod.
- Add simple mod-side physics: a ballistic fall from hand velocity plus a ground/collision ray, then rest. That could use a
  native raycast later if one is evidenced.
- It needs no native writes, has exactly the right mesh, and keeps all state client-local and under mod control (cap the
  count and fade old props).
- Gaps: the mod renders and owns these props, so native occlusion, lighting, shadows and decals are not native. Ground
  contact needs a collision query, or the prop must be placed at a measured floor height.

**Native alternative: ClientDynamicModelEntity.** It is feasible but has a mesh problem. The data needs a *rigid*
MeshAsset plus a HavokAsset, and the magazine exists only as skinned-weapon sub-geometry, so:
- (a) reusing any loaded DynamicModelEntityData shows the wrong mesh;
- (b) fabricating EntityData, MeshAsset and HavokAsset at runtime means forging refcounted DataContainers and resource
  ownership, and is unsupported.

Further risks:
- Thread affinity: these calls must run on the client update thread (INF).
- Factory side effects on info+0 (the world), and partition and resource lifetime when the level unloads.
- Each entity must be destroyed explicitly (0x62d810) before teardown.
- None of this is runtime-validated.

The server PickupEntity route replicates, is interactable, and expires on TimeToLive, so it is not appropriate.
**Verdict: mod-rendered.** Keep native spawning disabled, with the status "native rigid magazine asset absent".
