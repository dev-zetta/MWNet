#include "recastmeshmanager.hpp"
#include "recastmeshbuilder.hpp"
#include <components/resource/bulletshape.hpp>

namespace DetourNavigator
{
    RecastMeshManager::RecastMeshManager(const Settings& settings, const TileBounds& bounds, std::size_t generation)
        : mSettings(settings)
        , mGeneration(generation)
        , mTileBounds(bounds)
    {
    }

    bool RecastMeshManager::addObject(const ObjectId id, const CollisionShape& shape, const btTransform& transform,
                                      const AreaType areaType)
    {
        const std::lock_guard lock(mMutex);
        const auto object = mObjects.lower_bound(id);
        if (object != mObjects.end() && object->first == id)
            return false;
        const auto iterator = mObjectsOrder.emplace(mObjectsOrder.end(),
            OscillatingRecastMeshObject(RecastMeshObject(shape, transform, areaType), mRevision + 1));
        mObjects.emplace_hint(object, id, iterator);
        ++mRevision;
        return true;
    }

    bool RecastMeshManager::updateObject(const ObjectId id, const btTransform& transform, const AreaType areaType)
    {
        const std::lock_guard lock(mMutex);
        const auto object = mObjects.find(id);
        if (object == mObjects.end())
            return false;
        const std::size_t lastChangeRevision = mLastNavMeshReportedChange.has_value()
                ? mLastNavMeshReportedChange->mRevision : mRevision;
        if (!object->second->update(transform, areaType, lastChangeRevision, mTileBounds))
            return false;
        ++mRevision;
        return true;
    }

    std::optional<RemovedRecastMeshObject> RecastMeshManager::removeObject(const ObjectId id)
    {
        const std::lock_guard lock(mMutex);
        const auto object = mObjects.find(id);
        if (object == mObjects.end())
            return std::nullopt;
        const RemovedRecastMeshObject result {object->second->getImpl().getShape(), object->second->getImpl().getTransform()};
        mObjectsOrder.erase(object->second);
        mObjects.erase(object);
        ++mRevision;
        return result;
    }

    bool RecastMeshManager::addWater(const osg::Vec2i& cellPosition, const int cellSize,
        const btTransform& transform)
    {
        const std::lock_guard lock(mMutex);
        const auto iterator = mWaterOrder.emplace(mWaterOrder.end(), Water {cellSize, transform});
        if (!mWater.emplace(cellPosition, iterator).second)
        {
            mWaterOrder.erase(iterator);
            return false;
        }
        ++mRevision;
        return true;
    }

    std::optional<RecastMeshManager::Water> RecastMeshManager::removeWater(const osg::Vec2i& cellPosition)
    {
        const std::lock_guard lock(mMutex);
        const auto water = mWater.find(cellPosition);
        if (water == mWater.end())
            return std::nullopt;
        ++mRevision;
        const auto result = *water->second;
        mWaterOrder.erase(water->second);
        mWater.erase(water);
        return result;
    }

    std::shared_ptr<RecastMesh> RecastMeshManager::getMesh()
    {
        RecastMeshBuilder builder(mTileBounds);
        using Object = std::tuple<
            osg::ref_ptr<const Resource::BulletShapeInstance>,
            std::reference_wrapper<const btCollisionShape>,
            btTransform,
            AreaType,
            ObjectTransform
        >;
        std::vector<Object> objects;
        std::vector<std::pair<osg::Vec2i, DetourNavigator::Water>> waters;
        std::size_t revision;
        {
            const std::lock_guard lock(mMutex);
            for (const auto& [pos, it] : mWater)
                waters.emplace_back(pos, DetourNavigator::Water{it->mCellSize, it->mTransform.getOrigin().y()});
            objects.reserve(mObjectsOrder.size());
            for (const auto& object : mObjectsOrder)
            {
                const auto& impl = object.getImpl();
                objects.emplace_back(impl.getInstance(), impl.getShape(), impl.getTransform(), impl.getAreaType(), impl.getObjectTransform());
            }
            revision = mRevision;
        }
        for (const auto& [pos, water] : waters)
            builder.addWater(pos, water);
        for (const auto& [instance, shape, transform, areaType, objectTransform] : objects)
            builder.addObject(shape, transform, areaType, instance, objectTransform);
        return std::move(builder).create(Version{mGeneration, revision});
    }

    bool RecastMeshManager::isEmpty() const
    {
        const std::lock_guard lock(mMutex);
        return mObjects.empty();
    }

    void RecastMeshManager::reportNavMeshChange(Version recastMeshVersion, Version navMeshVersion)
    {
        if (recastMeshVersion.mGeneration != mGeneration)
            return;
        const std::lock_guard lock(mMutex);
        if (mLastNavMeshReport.has_value() && navMeshVersion < mLastNavMeshReport->mNavMeshVersion)
            return;
        mLastNavMeshReport = {recastMeshVersion.mRevision, navMeshVersion};
        if (!mLastNavMeshReportedChange.has_value()
                || mLastNavMeshReportedChange->mNavMeshVersion < mLastNavMeshReport->mNavMeshVersion)
            mLastNavMeshReportedChange = mLastNavMeshReport;
    }

    Version RecastMeshManager::getVersion() const
    {
        const std::lock_guard lock(mMutex);
        return Version {mGeneration, mRevision};
    }
}
