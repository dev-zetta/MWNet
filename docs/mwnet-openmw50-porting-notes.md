# MWNet → OpenMW 0.50 Porting Notes

This document describes all critical changes and refactoring required to make MWNet additions
compile and function correctly against the OpenMW 0.50 branch. It is intended as a reference
for future merges and maintenance.

---

## Table of Contents

1. [Build System (CMakeLists)](#1-build-system-cmakelists)
2. [Type System: std::string → ESM::RefId](#2-type-system-stdstring--esmrefid)
3. [CellRef: ESM4 Variant Handling](#3-cellref-esm4-variant-handling)
4. [DetourNavigator: RecastMeshManager API](#4-detournavigator-recastmeshmanager-api)
5. [Physics: HeightField OSG Object](#5-physics-heightfield-osg-object)
6. [Physics: setPhysicsFramerate](#6-physics-setphysicsframerate)
7. [Rendering: GlobalMap::setImage](#7-rendering-globalmapsetimage)
8. [GUI: interactiveMessageBox with hasServerOrigin](#8-gui-interactivemessagebox-with-hasserverorigin)
9. [Qt Compatibility (Qt5 / Qt6)](#9-qt-compatibility-qt5--qt6)
10. [Boost → std Filesystem](#10-boost--std-filesystem)
11. [Settings API Changes](#11-settings-api-changes)
12. [Linker / Library Changes](#12-linker--library-changes)

---

## 1. Build System (CMakeLists)

### `components/CMakeLists.txt`

**Problem:** Several components added or reorganised in OpenMW 0.50 were missing from the build,
and MWNet-specific components needed to be guarded against server-only builds.

**Changes:**

- Added `lua_ui`, `esmloader`, `sqlite3` component directories inside the
  `IF (BUILD_OPENMW OR BUILD_OPENCS)` guard (client-only).
- Added the full `detournavigator` source file list inside the same guard. OpenMW 0.50 split
  navigation into many more files; all must be listed explicitly:
  `asyncnavmeshupdater`, `cachedrecastmeshmanager`, `commulativeaabb`, `debug`,
  `findrandompointaroundcircle`, `findsmoothpath`, `generatenavmeshtile`, `gettilespositions`,
  `makenavmesh`, `navigator`, `navigatorimpl`, `navigatorutils`, `navmeshcacheitem`,
  `navmeshdb`, `navmeshdbutils`, `navmeshmanager`, `navmeshtilescache`, `navmeshtileview`,
  `offmeshconnectionsmanager`, `oscillatingrecastmeshobject`, `preparednavmeshdata`, `raycast`,
  `recast`, `recastcontext`, `recastmesh`, `recastmeshbuilder`, `recastmeshmanager`,
  `recastmeshobject`, `serialization`, `settings`, `stats`, `tilecachedrecastmeshmanager`.
- `collisionshapetype` is added **outside** the guard (needed by server too).
- Added `files/qtconversion` to `add_component_qt_dir` (provides `Files::pathToQString` /
  `pathFromQString` used by launcher and other Qt targets).
- Added `smhasher` and `SQLite::SQLite3` to `target_link_libraries(components ...)`.
- Replaced legacy `${Boost_FILESYSTEM_LIBRARY}` etc. with imported targets
  `Boost::filesystem`, `Boost::program_options`, `Boost::iostreams`, `Boost::system`.

### `apps/openmw/CMakeLists.txt`

- Wrapped `openmw-lib` in `--start-group` / `--end-group` when linking `mwnet` to resolve
  circular static library dependencies introduced by the merge.
- Added `ICU::uc`, `ICU::i18n`, `ICU::data` to `openmw-lib` link libraries (required by
  OpenMW 0.50's ESM layer).

### `apps/openmw-mp/CMakeLists.txt`

- Replaced legacy `${Boost_*_LIBRARY}` variables with imported targets.
- Added `smhasher` to link libraries.
- Removed invalid `-ldetournavigator` (no longer a separate shared library).

### `apps/browser/CMakeLists.txt`

- Fixed UI file paths from `${CMAKE_SOURCE_DIR}/files/ui/` to
  `${CMAKE_SOURCE_DIR}/files/mwnet/ui/`.
- Added `components_qt`, `smhasher`, `Qt5::Svg` to link libraries.
- Replaced `AUTOMOC`/`AUTOUIC`/`AUTORCC` with explicit `moc`/`uic`/`rcc` custom commands
  (required because the browser target is not an `openmw_add_executable` target).

### `apps/launcher/CMakeLists.txt`

- Added `components`, `smhasher`, `Boost::filesystem`, `Boost::program_options` to link
  libraries.

### `apps/essimporter/CMakeLists.txt`, `apps/niftest/CMakeLists.txt`

- Added `smhasher` to link libraries (pulled in transitively by `components` in 0.50).

---

## 2. Type System: std::string → ESM::RefId

OpenMW 0.50 replaced most `std::string` identifiers with `ESM::RefId`. All MWNet additions
that passed or stored record IDs as `std::string` must be adapted.

### Conversion helpers

| Old | New |
|-----|-----|
| `std::string id` | `ESM::RefId id` |
| `id` (pass to API) | `id` (direct) |
| `std::string` from RefId | `id.getRefIdString()` |
| `std::string` to RefId | `ESM::RefId::stringRefId(str)` |

### Affected MWNet additions

**`apps/openmw/mwworld/worldimp.hpp/.cpp` — `hasGlobal`, `createGlobal`**

```cpp
// 0.8.1 original
bool World::hasGlobal(const std::string& name)
{ return mGlobalVariables.hasRecord(name); }

// openmw-50 adaptation
bool World::hasGlobal(const ESM::RefId& name)
{ return mGlobalVariables.hasRecord(name.getRefIdString()); }
```

```cpp
// 0.8.1 original
void World::createGlobal(const std::string& name, ESM::VarType varType)
{ ESM::Global g; g.mId = name; ... }

// openmw-50 adaptation
void World::createGlobal(const ESM::RefId& name, ESM::VarType varType)
{ ESM::Global g; g.mId = name; ... }  // ESM::Global::mId is now ESM::RefId
```

**`apps/openmw/mwgui/windowmanagerimp.hpp/.cpp` — `executeCommandInConsole`**

```cpp
// 0.8.1 original
void WindowManager::executeCommandInConsole(const std::string& command)
{ mConsole->execute(command); }

// openmw-50 adaptation
void WindowManager::executeCommandInConsole(const ESM::RefId& command)
{ mConsole->execute(command.getRefIdString()); }
```

**`apps/openmw/mwdialogue/dialoguemanagerimp.hpp/.cpp` — `getVoiceCaption`**

```cpp
// 0.8.1 original
std::string DialogueManager::getVoiceCaption(const std::string& sound) const

// openmw-50 adaptation
ESM::RefId DialogueManager::getVoiceCaption(const ESM::RefId& sound) const
// Uses sound.getRefIdString() for ciEqual comparison
// Returns ESM::RefId::stringRefId(infoIter->mResponse)
```

**`apps/openmw/mwmechanics/mechanicsmanagerimp.hpp/.cpp` — `isBoundItem`**

```cpp
// 0.8.1 original
bool MechanicsManager::isBoundItem(const std::string& itemId)

// openmw-50 adaptation
bool MechanicsManager::isBoundItem(const ESM::RefId& itemId)
// cache lookup uses ESM::RefId directly
```

---

## 3. CellRef: ESM4 Variant Handling

OpenMW 0.50 introduced ESM4 support. `CellRef` now wraps an `ESM::ReferenceVariant` (a
`std::variant` of `ESM::CellRef`, `ESM4::Reference`, `ESM4::ActorCharacter`). All MWNet
additions that accessed `mCellRef.*` directly must use `std::visit`.

### `setRefNum(unsigned int)` — `apps/openmw/mwworld/cellref.cpp`

```cpp
// 0.8.1 original (direct access)
void CellRef::setRefNum(unsigned int index)
{ mCellRef.mRefNum.mIndex = index; }

// openmw-50 adaptation (variant visit)
void CellRef::setRefNum(unsigned int index)
{
    std::visit(ESM::VisitOverload{
        [&](ESM4::Reference& ref)      { ref.mId.mIndex = index; },
        [&](ESM4::ActorCharacter& ref) { ref.mId.mIndex = index; },
        [&](ESM::CellRef& ref)         { ref.mRefNum.mIndex = index; },
    }, mCellRef.mVariant);
}
```

### `getMpNum` / `setMpNum` — `apps/openmw/mwworld/cellref.cpp`

```cpp
// openmw-50 adaptation
unsigned int CellRef::getMpNum() const
{
    return std::visit(ESM::VisitOverload{
        [](const ESM::CellRef& ref) -> unsigned int { return ref.mMpNum; },
        [](const ESM4::Reference&)  -> unsigned int { return 0; },
        [](const ESM4::ActorCharacter&) -> unsigned int { return 0; },
    }, mCellRef.mVariant);
}
```

### `setTeleport` — `apps/openmw/mwworld/cellref.cpp`

```cpp
void CellRef::setTeleport(bool teleportState)
{
    std::visit(ESM::VisitOverload{
        [teleportState](ESM::CellRef& ref) { ref.mTeleport = teleportState; },
        [](ESM4::Reference&) {},
        [](ESM4::ActorCharacter&) {},
    }, mCellRef.mVariant);
}
```

The same pattern applies to `setDoorDest` and `setDestCell`.

---

## 4. DetourNavigator: RecastMeshManager API

OpenMW 0.50 significantly refactored the navigation mesh system. `RecastMeshManager` (used by
`CachedRecastMeshManager` for per-tile mesh building) has a different API.

### `getMesh()` — `components/detournavigator/recastmeshmanager.cpp`

**`RecastMeshBuilder` constructor**

```cpp
// 0.8.1 original
RecastMeshBuilder builder(mSettings, mTileBounds);

// openmw-50: Settings argument removed
RecastMeshBuilder builder(mTileBounds);
```

**Object tuple and `addObject` call**

```cpp
// 0.8.1 original
using Object = std::tuple<
    osg::ref_ptr<const osg::Object>,   // holder
    std::reference_wrapper<const btCollisionShape>,
    btTransform,
    AreaType
>;
// ...
objects.emplace_back(impl.getHolder(), impl.getShape(), impl.getTransform(), impl.getAreaType());
// ...
builder.addObject(shape, transform, areaType);  // 3-arg overload
```

```cpp
// openmw-50 adaptation
using Object = std::tuple<
    osg::ref_ptr<const Resource::BulletShapeInstance>,  // instance (not holder)
    std::reference_wrapper<const btCollisionShape>,
    btTransform,
    AreaType,
    ObjectTransform                                      // new argument
>;
// ...
objects.emplace_back(impl.getInstance(), impl.getShape(), impl.getTransform(),
                     impl.getAreaType(), impl.getObjectTransform());
// ...
builder.addObject(shape, transform, areaType, instance, objectTransform);  // 5-arg overload
```

**Water handling**

```cpp
// 0.8.1 original
// Iterated mWaterOrder directly, called builder.addWater(cellSize, transform)
for (const auto& v : mWaterOrder)
    builder.addWater(v.mCellSize, v.mTransform);

// openmw-50 adaptation
// RecastMesh::Water uses mLevel (float) not mTransform (btTransform)
// Extract Y origin as water height; pass cell position separately
for (const auto& [pos, it] : mWater)
    waters.emplace_back(pos, DetourNavigator::Water{it->mCellSize, it->mTransform.getOrigin().y()});
// ...
builder.addWater(pos, water);  // now takes (osg::Vec2i, Water)
```

**`create()` call**

```cpp
// 0.8.1 original
return std::move(builder).create(mGeneration, revision);

// openmw-50 adaptation
return std::move(builder).create(Version{mGeneration, revision});
```

**Required new include**

```cpp
#include <components/resource/bulletshape.hpp>  // for BulletShapeInstance
```

---

## 5. Physics: HeightField OSG Object

Upstream 0.8.1 made `HeightField` inherit from `osg::Object` (via `META_Object` macro) to allow
OSG scene graph cloning. OpenMW 0.50 removed this inheritance.

### `apps/openmw/mwphysics/heightfield.hpp`

The merged file retains the MWNet addition:

```cpp
// MWNet addition kept in merged file
class HeightField : public osg::Object
{
public:
    // ... normal constructor (signature changed in openmw-50: int size/verts not float) ...
    META_Object(MWPhysics, HeightField)
    // ...
private:
    HeightField();
    HeightField(const HeightField&, const osg::CopyOp&);
    void operator=(const HeightField&);
    HeightField(const HeightField&);
};
```

**Constructor signature change** (openmw-50 changed `float triSize, float sqrtVerts` to
`int size, int verts`):

```cpp
// 0.8.1
HeightField(const float* heights, int x, int y, float triSize, float sqrtVerts, ...)

// openmw-50
HeightField(const float* heights, int x, int y, int size, int verts, ...)
```

### `apps/openmw/mwphysics/heightfield.cpp`

The `META_Object` macro requires default and copy constructors to be defined. Add them with
empty bodies (they are never called for functional instances):

```cpp
HeightField::HeightField()
    : mTaskScheduler(nullptr)
{}

HeightField::HeightField(const HeightField& other, const osg::CopyOp&)
    : osg::Object(other)
    , mTaskScheduler(other.mTaskScheduler)
{}
```

---

## 6. Physics: setPhysicsFramerate

### `apps/openmw/mwphysics/mtphysics.hpp`

In 0.8.1, `PhysicsTaskScheduler::mPhysicsDt` was made public via a MWNet addition. In
openmw-50 it is private. Add a setter instead:

```cpp
// Add to PhysicsTaskScheduler public interface
void setPhysicsDt(float physicsDt) { mPhysicsDt = physicsDt; }
```

### `apps/openmw/mwphysics/physicssystem.cpp`

```cpp
// 0.8.1 original
void PhysicsSystem::setPhysicsFramerate(float physFramerate)
{
    if (physFramerate > 0 && physFramerate < 100)
    {
        mPhysicsDt = 1.f / physFramerate;
        mTaskScheduler->mPhysicsDt = mPhysicsDt;  // direct public access
        std::cerr << "Warning: ..." << std::endl;
    }
    else { std::cerr << "Warning: ..." << std::endl; }
}

// openmw-50 adaptation
void PhysicsSystem::setPhysicsFramerate(float physFramerate)
{
    if (physFramerate > 0 && physFramerate < 100)
    {
        mPhysicsDt = 1.f / physFramerate;
        mTaskScheduler->setPhysicsDt(mPhysicsDt);  // use new setter
        Log(Debug::Warning) << "Warning: physics framerate was overridden ...";
    }
    else
    {
        Log(Debug::Warning) << "Warning: attempted to override physics framerate ...";
    }
}
```

---

## 7. Rendering: GlobalMap::setImage

### `apps/openmw/mwrender/globalmap.cpp`

The `mCellSize` member was removed from `GlobalMap` in openmw-50 (replaced by
`Settings::map().mGlobalMapCellSize`). The implementation must be adapted accordingly.

```cpp
// 0.8.1 original
void GlobalMap::setImage(int cellX, int cellY, const std::vector<char>& imageData)
{
    Files::IMemStream istream(&imageData[0], imageData.size());
    osgDB::ReaderWriter* reader = osgDB::Registry::instance()->getReaderWriterForExtension("png");
    // ... decode PNG ...
    int posX = (cellX - mMinX) * mCellSize;          // mCellSize was a GlobalMap member
    int posY = (cellY - mMinY + 1) * mCellSize;
    // ... bounds check ...
    requestOverlayTextureUpdate(posX, mHeight - posY, mCellSize, mCellSize, texture, true, false);
}

// openmw-50 adaptation
void GlobalMap::setImage(int cellX, int cellY, const std::vector<char>& imageData)
{
    Files::IMemStream istream(imageData.data(), imageData.size());
    osgDB::ReaderWriter* reader = osgDB::Registry::instance()->getReaderWriterForExtension("png");
    if (!reader) { Log(Debug::Error) << "..."; return; }
    osgDB::ReaderWriter::ReadResult result = reader->readImage(istream);
    if (!result.success()) { Log(Debug::Error) << "..."; return; }

    osg::ref_ptr<osg::Image> image = result.getImage();

    const int cellSize = Settings::map().mGlobalMapCellSize;  // replaces mCellSize member
    int posX = (cellX - mMinX) * cellSize;
    int posY = (cellY - mMinY + 1) * cellSize;

    if (cellX > mMaxX || cellX < mMinX || cellY > mMaxY || cellY < mMinY)
        return;

    osg::ref_ptr<osg::Texture2D> texture(new osg::Texture2D);
    texture->setImage(image);
    // ... wrap/filter settings ...
    requestOverlayTextureUpdate(posX, mHeight - posY, cellSize, cellSize, texture, true, false);
}
```

**Required new includes in `globalmap.cpp`:**

```cpp
#include <osgDB/ReadFile>
#include <osgDB/Registry>
```

---

## 8. GUI: interactiveMessageBox with hasServerOrigin

### `apps/openmw/mwgui/windowmanagerimp.cpp`

The MWNet overload of `interactiveMessageBox` takes `hasServerOrigin` to allow the server to
display message boxes on clients. In 0.8.1 it called the string overload directly; in openmw-50
the block loop internals changed (uses `FrameRateLimiter` and `mViewer` directly).

```cpp
// 0.8.1 original
void WindowManager::interactiveMessageBox(const std::string& message,
    const std::vector<std::string>& buttons, bool block, bool hasServerOrigin)
{
    mMessageBoxManager->createInteractiveMessageBox(message, buttons, hasServerOrigin);
    updateVisible();
    if (block) { /* simple loop */ }
}

// openmw-50 adaptation
void WindowManager::interactiveMessageBox(const ESM::RefId& message,
    const std::vector<ESM::RefId>& buttons, bool block, bool hasServerOrigin)
{
    // Convert RefId arguments to strings
    std::vector<std::string> buttonStrings;
    for (const auto& b : buttons)
        buttonStrings.push_back(b.getRefIdString());

    // Pass hasServerOrigin; createInteractiveMessageBox signature now has defaultFocus param
    mMessageBoxManager->createInteractiveMessageBox(
        message.getRefIdString(), buttonStrings, block, -1, hasServerOrigin);
    updateVisible();

    if (block)
    {
        // Must use FrameRateLimiter + mViewer pattern matching openmw-50's own overload
        Misc::FrameRateLimiter frameRateLimiter =
            Misc::makeFrameRateLimiter(MWBase::Environment::get().getFrameRateLimit());
        while (mMessageBoxManager->readPressedButton(false) == -1
            && !MWBase::Environment::get().getStateManager()->hasQuitRequest())
        {
            const double dt = std::chrono::duration_cast<std::chrono::duration<double>>(
                frameRateLimiter.getLastFrameDuration()).count();
            mKeyboardNavigation->onFrame();
            mMessageBoxManager->onFrame(dt);
            MWBase::Environment::get().getInputManager()->update(dt, true, false);
            if (!mWindowVisible)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            else
            {
                mViewer->eventTraversal();
                mViewer->updateTraversal();
                mViewer->renderingTraversals();
            }
            mViewer->advance(mViewer->getFrameStamp()->getSimulationTime());
            frameRateLimiter.limit();
        }
        mMessageBoxManager->resetInteractiveMessageBox();
    }
}
```

---

## 9. Qt Compatibility (Qt5 / Qt6)

OpenMW 0.50 added Qt6 support. MWNet additions using Qt APIs must guard version-specific calls.

### `components/misc/utf8qtextstream.hpp`

```cpp
inline void ensureUtf8Encoding(QTextStream& stream)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
}
```

### `components/l10n/qttranslations.cpp`

```cpp
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    auto qtPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
    auto qtPath = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
```

---

## 10. Boost → std Filesystem

OpenMW 0.50 migrated from `boost::filesystem` to `std::filesystem` in most places. MWNet
code that used `boost::filesystem` for path handling must be adapted where it interfaces with
OpenMW APIs.

### `components/files/escape.cpp` — `EscapePath::toPathContainer`

```cpp
// 0.8.1 original
PathContainer result;
for (const auto& ep : escapePathContainer)
    result.push_back(ep.mPath);  // mPath was boost::filesystem::path

// openmw-50 adaptation
PathContainer result;  // PathContainer is now std::vector<std::filesystem::path>
for (const auto& ep : escapePathContainer)
    result.push_back(std::filesystem::path(ep.mPath.string()));
    // ep.mPath is still boost::filesystem::path (used in escape filter internals)
    // convert via .string() to std::filesystem::path
```

### `apps/launcher/importpage.cpp`

```cpp
// 0.8.1 original
QFile file(QString::fromStdString(path.string()));  // path was boost::filesystem::path

// openmw-50: path is std::filesystem::path — same call works
QFile file(QString::fromStdString(path.string()));
```

### `apps/browser/main.cpp`

```cpp
// 0.8.1 original
Settings::Manager::load(mCfgMgr);  // returned void

// openmw-50: returns std::filesystem::path (log path)
std::string loadSettings()
{
    Files::ConfigurationManager mCfgMgr;
    return Settings::Manager::load(mCfgMgr).string();
}
```

---

## 11. Settings API Changes

OpenMW 0.50 replaced the `Settings::Manager::getString/getInt/getFloat` free functions with
typed settings structs accessed via `Settings::values()` or category-specific accessors.

### Global map cell size

```cpp
// 0.8.1 original (GlobalMap member, hardcoded by MWNet)
mCellSize = 18;

// openmw-50: no mCellSize member; use settings accessor
const int cellSize = Settings::map().mGlobalMapCellSize;
```

### Browser / server settings

```cpp
// 0.8.1 original
std::string addr = Settings::Manager::getString("address", "Master");
int port = Settings::Manager::getInt("port", "Master");

// openmw-50: Settings::Manager static getters still exist for legacy sections
// No change needed for MWNet-specific cfg sections not covered by typed structs
```

---

## 12. Linker / Library Changes

### smhasher

OpenMW 0.50 uses `smhasher` (MurmurHash) internally via `components`. Any target that links
`components` and uses hashing must also explicitly link `smhasher`:

```cmake
target_link_libraries(my_target smhasher)
```

Affected targets: `mwnet-server`, `mwnet-browser`, `openmw-launcher`,
`openmw-essimporter`, `niftest`.

### SQLite3

`navmeshdb` (DetourNavigator) requires SQLite3:

```cmake
target_link_libraries(components ... SQLite::SQLite3)
```

### ICU

OpenMW 0.50's ESM layer uses ICU for Unicode handling:

```cmake
target_link_libraries(openmw-lib ICU::uc ICU::i18n ICU::data)
```

### Circular static library dependencies

The `mwnet` executable links `openmw-lib` (a large static library with internal circular
dependencies). Wrap it in a linker group:

```cmake
target_link_libraries(mwnet
    -Wl,--start-group
    openmw-lib
    -Wl,--end-group
)
```

---

## Summary Table

| Area | 0.8.1 API | openmw-50 API | Action |
|------|-----------|---------------|--------|
| Record IDs | `std::string` | `ESM::RefId` | Use `.getRefIdString()` / `ESM::RefId::stringRefId()` |
| CellRef fields | Direct `mCellRef.*` | `std::visit` on variant | Wrap all access in `std::visit(ESM::VisitOverload{...})` |
| RecastMeshBuilder ctor | `(Settings, TileBounds)` | `(TileBounds)` | Remove Settings argument |
| `addObject` | `(shape, transform, areaType)` | `(shape, transform, areaType, instance, objectTransform)` | Add instance + ObjectTransform |
| `addWater` | `(cellSize, btTransform)` | `(osg::Vec2i, Water{cellSize, float level})` | Extract Y from transform as level |
| `create()` | `(generation, revision)` | `(Version{generation, revision})` | Wrap in Version struct |
| `getHolder()` | Returns `osg::Object*` | Replaced by `getInstance()` returning `BulletShapeInstance*` | Use `getInstance()` |
| `GlobalMap::mCellSize` | Member variable | Removed | Use `Settings::map().mGlobalMapCellSize` |
| `PhysicsTaskScheduler::mPhysicsDt` | Public | Private | Add `setPhysicsDt()` setter |
| `HeightField` ctor args | `float triSize, float sqrtVerts` | `int size, int verts` | Update all call sites |
| Qt text codec | `stream.setCodec("UTF-8")` | `stream.setEncoding(QStringConverter::Utf8)` | Guard with `QT_VERSION_CHECK(6,0,0)` |
| Qt library path | `QLibraryInfo::location()` | `QLibraryInfo::path()` | Guard with `QT_VERSION_CHECK(6,0,0)` |
| Settings load | Returns `void` | Returns `std::filesystem::path` | Capture return value if needed |
| Boost filesystem | `boost::filesystem::path` | `std::filesystem::path` | Convert via `.string()` at boundaries |
