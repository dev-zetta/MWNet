#include "load.hpp"
#include "esmdata.hpp"
#include "lessbyid.hpp"
#include "record.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm/defs.hpp>
#include <components/esm/typetraits.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/files/collections.hpp>
#include <components/files/conversion.hpp>
#include <components/files/multidircollection.hpp>
#include <components/loadinglistener/loadinglistener.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/lower.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace EsmLoader
{
    namespace
    {
        struct GetKey
        {
            template <class T>
            decltype(auto) operator()(const T& v) const
            {
                return (v.mId);
            }

            const ESM::RefId& operator()(const ESM::Cell& v) const { return v.mId; }

            std::pair<int, int> operator()(const ESM::Land& v) const { return std::pair(v.mX, v.mY); }

            template <class T>
            decltype(auto) operator()(const Record<T>& v) const
            {
                return (*this)(v.mValue);
            }
        };

        struct CellRecords
        {
            Records<ESM::Cell> mValues;
            std::map<std::string, std::size_t> mByName;
            std::map<std::pair<int, int>, std::size_t> mByPosition;
        };

        template <class T>
        concept NotHasId = !ESM::HasId<T>;

        template <ESM::HasId T>
        void loadRecord(ESM::ESMReader& reader, Records<T>& records)
        {
            T record;
            bool deleted = false;
            record.load(reader, deleted);
            if (Misc::ResourceHelpers::isHiddenMarker(record.mId))
                return;
            records.emplace_back(deleted, std::move(record));
        }

        template <NotHasId T>
        void loadRecord(ESM::ESMReader& reader, Records<T>& records)
        {
            T record;
            bool deleted = false;
            record.load(reader, deleted);
            records.emplace_back(deleted, std::move(record));
        }

        void loadRecord(ESM::ESMReader& reader, CellRecords& records)
        {
            ESM::Cell record;
            bool deleted = false;
            record.loadNameAndData(reader, deleted);

            if ((record.mData.mFlags & ESM::Cell::Interior) != 0)
            {
                const auto it = records.mByName.find(record.mName);
                if (it == records.mByName.end())
                {
                    record.loadCell(reader, true);
                    records.mByName.emplace_hint(it, record.mName, records.mValues.size());
                    records.mValues.emplace_back(deleted, std::move(record));
                }
                else
                {
                    Record<ESM::Cell>& old = records.mValues[it->second];
                    old.mValue.mData = record.mData;
                    old.mValue.loadCell(reader, true);
                }
            }
            else
            {
                const std::pair<int, int> position(record.mData.mX, record.mData.mY);
                const auto it = records.mByPosition.find(position);
                if (it == records.mByPosition.end())
                {
                    record.loadCell(reader, true);
                    records.mByPosition.emplace_hint(it, position, records.mValues.size());
                    records.mValues.emplace_back(deleted, std::move(record));
                }
                else
                {
                    Record<ESM::Cell>& old = records.mValues[it->second];
                    old.mValue.mData = record.mData;
                    old.mValue.loadCell(reader, true);
                }
            }
        }

        struct ShallowContent
        {
            Records<ESM::Activator> mActivators;
            CellRecords mCells;
            Records<ESM::Container> mContainers;
            Records<ESM::Door> mDoors;
            Records<ESM::GameSetting> mGameSettings;
            Records<ESM::Class> mClasses;
            Records<ESM::Creature> mCreatures;
            Records<ESM::Enchantment> mEnchantments;
            Records<ESM::Ingredient> mIngredients;
            Records<ESM::MagicEffect> mMagicEffects;
            Records<ESM::Potion> mPotions;
            Records<ESM::Spell> mSpells;
            Records<ESM::NPC> mNpcs;
            Records<ESM::Race> mRaces;
            Records<ESM::Skill> mSkills;
            Records<ESM::Armor> mArmors;
            Records<ESM::Book> mBooks;
            Records<ESM::Clothing> mClothing;
            Records<ESM::Weapon> mWeapons;
            Records<ESM::Land> mLands;
            Records<ESM::Static> mStatics;
        };

        void loadRecord(const Query& query, const ESM::NAME& name, ESM::ESMReader& reader, ShallowContent& content)
        {
            switch (name.toInt())
            {
                case ESM::REC_ACTI:
                    if (query.mLoadActivators)
                        return loadRecord(reader, content.mActivators);
                    break;
                case ESM::REC_CELL:
                    if (query.mLoadCells)
                        return loadRecord(reader, content.mCells);
                    break;
                case ESM::REC_CONT:
                    if (query.mLoadContainers)
                        return loadRecord(reader, content.mContainers);
                    break;
                case ESM::REC_DOOR:
                    if (query.mLoadDoors)
                        return loadRecord(reader, content.mDoors);
                    break;
                case ESM::REC_GMST:
                    if (query.mLoadGameSettings)
                        return loadRecord(reader, content.mGameSettings);
                    break;
                case ESM::REC_CLAS:
                    if (query.mLoadActorMagic)
                        return loadRecord(reader, content.mClasses);
                    break;
                case ESM::REC_CREA:
                    if (query.mLoadActorMagic)
                        return loadRecord(reader, content.mCreatures);
                    break;
                case ESM::REC_NPC_:
                    if (query.mLoadActorMagic)
                        return loadRecord(reader, content.mNpcs);
                    break;
                case ESM::REC_RACE:
                    if (query.mLoadActorMagic)
                        return loadRecord(reader, content.mRaces);
                    break;
                case ESM::REC_SKIL:
                    if (query.mLoadActorMagic)
                        return loadRecord(reader, content.mSkills);
                    break;
                case ESM::REC_ENCH:
                    if (query.mLoadMagic)
                        return loadRecord(reader, content.mEnchantments);
                    break;
                case ESM::REC_INGR:
                    if (query.mLoadMagic)
                        return loadRecord(reader, content.mIngredients);
                    break;
                case ESM::REC_MGEF:
                    if (query.mLoadMagic)
                        return loadRecord(reader, content.mMagicEffects);
                    break;
                case ESM::REC_SPEL:
                    if (query.mLoadMagic)
                        return loadRecord(reader, content.mSpells);
                    break;
                case ESM::REC_ALCH:
                    if (query.mLoadMagic)
                        return loadRecord(reader, content.mPotions);
                    break;
                case ESM::REC_ARMO:
                    if (query.mLoadMagic)
                        return loadRecord(reader, content.mArmors);
                    break;
                case ESM::REC_BOOK:
                    if (query.mLoadMagic)
                        return loadRecord(reader, content.mBooks);
                    break;
                case ESM::REC_CLOT:
                    if (query.mLoadMagic)
                        return loadRecord(reader, content.mClothing);
                    break;
                case ESM::REC_WEAP:
                    if (query.mLoadMagic)
                        return loadRecord(reader, content.mWeapons);
                    break;
                case ESM::REC_LAND:
                    if (query.mLoadLands)
                        return loadRecord(reader, content.mLands);
                    break;
                case ESM::REC_STAT:
                    if (query.mLoadStatics)
                        return loadRecord(reader, content.mStatics);
                    break;
            }

            reader.skipRecord();
        }

        void loadEsm(const Query& query, ESM::ESMReader& reader, ShallowContent& content, Loading::Listener* listener)
        {
            Log(Debug::Info) << "Loading ESM file " << reader.getName();

            while (reader.hasMoreRecs())
            {
                const ESM::NAME recName = reader.getRecName();
                reader.getRecHeader();
                if (reader.getRecordFlags() & ESM::FLAG_Ignored)
                {
                    reader.skipRecord();
                    continue;
                }
                loadRecord(query, recName, reader, content);

                if (listener != nullptr)
                    listener->setProgress(fileProgress * reader.getFileOffset() / reader.getFileSize());
            }
        }

        ShallowContent shallowLoad(const Query& query, const std::vector<std::string>& contentFiles,
            const Files::Collections& fileCollections, ESM::ReadersCache& readers, ToUTF8::Utf8Encoder* encoder,
            Loading::Listener* listener)
        {
            ShallowContent result;

            const std::set<std::string_view, Misc::StringUtils::CiComp> supportedFormats{
                "esm",
                "esp",
                "omwgame",
                "omwaddon",
                "project",
            };

            for (std::size_t i = 0; i < contentFiles.size(); ++i)
            {
                const std::string& file = contentFiles[i];
                const std::string_view extension = Misc::getFileExtension(file);

                if (!supportedFormats.contains(extension))
                {
                    Log(Debug::Warning) << "Skipping unsupported content file: " << file;
                    continue;
                }

                if (listener != nullptr)
                {
                    listener->setLabel(file);
                    listener->setProgressRange(fileProgress);
                }

                const Files::MultiDirCollection& collection = fileCollections.getCollection(extension);

                const ESM::ReadersCache::BusyItem reader = readers.get(i);
                reader->setEncoder(encoder);
                reader->setIndex(static_cast<int>(i));
                reader->open(collection.getPath(file));
                if (query.mLoadCells)
                    reader->resolveParentFileIndices(readers);

                loadEsm(query, *reader, result, listener);
            }

            return result;
        }

        struct WithType
        {
            ESM::RecNameInts mType;

            template <class T>
            RefIdWithType operator()(const T& v) const
            {
                return { v.mId, mType };
            }
        };

        template <class T>
        void addRefIdsTypes(const std::vector<T>& values, std::vector<RefIdWithType>& refIdsTypes)
        {
            std::transform(values.begin(), values.end(), std::back_inserter(refIdsTypes),
                WithType{ static_cast<ESM::RecNameInts>(T::sRecordId) });
        }

        void addRefIdsTypes(EsmData& content)
        {
            content.mRefIdTypes.reserve(content.mActivators.size() + content.mContainers.size() + content.mDoors.size()
                + content.mStatics.size());

            addRefIdsTypes(content.mActivators, content.mRefIdTypes);
            addRefIdsTypes(content.mContainers, content.mRefIdTypes);
            addRefIdsTypes(content.mDoors, content.mRefIdTypes);
            addRefIdsTypes(content.mStatics, content.mRefIdTypes);

            std::sort(content.mRefIdTypes.begin(), content.mRefIdTypes.end(), LessById{});
        }

        std::vector<ESM::Cell> prepareCellRecords(Records<ESM::Cell>& records)
        {
            std::vector<ESM::Cell> result;
            for (Record<ESM::Cell>& v : records)
                if (!v.mDeleted)
                    result.emplace_back(std::move(v.mValue));
            return result;
        }

        template <class T>
        void addEnchantedItems(Records<T>& records,
            bool consumable, std::vector<EnchantedItem>& result)
        {
            for (T& item : prepareRecords(records, GetKey{}))
            {
                if (!item.mEnchant.empty())
                    result.push_back({ std::move(item.mId),
                        std::move(item.mEnchant), consumable });
            }
        }
    }

    EsmData loadEsmData(const Query& query, const std::vector<std::string>& contentFiles,
        const Files::Collections& fileCollections, ESM::ReadersCache& readers, ToUTF8::Utf8Encoder* encoder,
        Loading::Listener* listener)
    {
        Log(Debug::Info) << "Loading ESM data...";

        ShallowContent content = shallowLoad(query, contentFiles, fileCollections, readers, encoder, listener);

        std::ostringstream loaded;

        if (query.mLoadActivators)
            loaded << ' ' << content.mActivators.size() << " activators,";
        if (query.mLoadCells)
            loaded << ' ' << content.mCells.mValues.size() << " cells,";
        if (query.mLoadContainers)
            loaded << ' ' << content.mContainers.size() << " containers,";
        if (query.mLoadDoors)
            loaded << ' ' << content.mDoors.size() << " doors,";
        if (query.mLoadGameSettings)
            loaded << ' ' << content.mGameSettings.size() << " game settings,";
        if (query.mLoadActorMagic)
            loaded << ' ' << content.mNpcs.size() << " NPCs,"
                   << ' ' << content.mCreatures.size() << " creatures,"
                   << ' ' << content.mRaces.size() << " races,"
                   << ' ' << content.mClasses.size() << " classes,"
                   << ' ' << content.mSkills.size() << " skills,";
        if (query.mLoadMagic)
            loaded << ' ' << content.mSpells.size() << " spells,"
                   << ' ' << content.mEnchantments.size() << " enchantments,"
                   << ' ' << content.mPotions.size() << " potions,"
                   << ' ' << content.mIngredients.size() << " ingredients,"
                   << ' ' << content.mMagicEffects.size() << " magic effects,";
        if (query.mLoadLands)
            loaded << ' ' << content.mLands.size() << " lands,";
        if (query.mLoadStatics)
            loaded << ' ' << content.mStatics.size() << " statics,";

        Log(Debug::Info) << "Loaded" << loaded.str();

        EsmData result;

        if (query.mLoadActivators)
            result.mActivators = prepareRecords(content.mActivators, GetKey{});
        if (query.mLoadCells)
            result.mCells = prepareCellRecords(content.mCells.mValues);
        if (query.mLoadContainers)
            result.mContainers = prepareRecords(content.mContainers, GetKey{});
        if (query.mLoadDoors)
            result.mDoors = prepareRecords(content.mDoors, GetKey{});
        if (query.mLoadGameSettings)
            result.mGameSettings = prepareRecords(content.mGameSettings, GetKey{});
        if (query.mLoadActorMagic)
        {
            result.mClasses = prepareRecords(content.mClasses, GetKey{});
            result.mCreatures = prepareRecords(content.mCreatures, GetKey{});
            result.mNpcs = prepareRecords(content.mNpcs, GetKey{});
            result.mRaces = prepareRecords(content.mRaces, GetKey{});
            result.mSkills = prepareRecords(content.mSkills, GetKey{});
        }
        if (query.mLoadMagic)
        {
            result.mEnchantments = prepareRecords(content.mEnchantments, GetKey{});
            result.mIngredients = prepareRecords(content.mIngredients, GetKey{});
            result.mMagicEffects = prepareRecords(content.mMagicEffects, GetKey{});
            result.mPotions = prepareRecords(content.mPotions, GetKey{});
            result.mSpells = prepareRecords(content.mSpells, GetKey{});
            addEnchantedItems(content.mArmors, false, result.mEnchantedItems);
            addEnchantedItems(content.mBooks, true, result.mEnchantedItems);
            addEnchantedItems(content.mClothing, false, result.mEnchantedItems);
            addEnchantedItems(content.mWeapons, false, result.mEnchantedItems);
            std::sort(result.mEnchantedItems.begin(), result.mEnchantedItems.end(),
                [](const EnchantedItem& left, const EnchantedItem& right) {
                    return left.mId < right.mId;
                });
        }
        if (query.mLoadLands)
            result.mLands = prepareRecords(content.mLands, GetKey{});
        if (query.mLoadStatics)
            result.mStatics = prepareRecords(content.mStatics, GetKey{});

        addRefIdsTypes(result);

        std::ostringstream prepared;

        if (query.mLoadActivators)
            prepared << ' ' << result.mActivators.size() << " unique activators,";
        if (query.mLoadCells)
            prepared << ' ' << result.mCells.size() << " unique cells,";
        if (query.mLoadContainers)
            prepared << ' ' << result.mContainers.size() << " unique containers,";
        if (query.mLoadDoors)
            prepared << ' ' << result.mDoors.size() << " unique doors,";
        if (query.mLoadGameSettings)
            prepared << ' ' << result.mGameSettings.size() << " unique game settings,";
        if (query.mLoadActorMagic)
            prepared << ' ' << result.mNpcs.size() << " unique NPCs,"
                     << ' ' << result.mCreatures.size() << " unique creatures,"
                     << ' ' << result.mRaces.size() << " unique races,"
                     << ' ' << result.mClasses.size() << " unique classes,"
                     << ' ' << result.mSkills.size() << " unique skills,";
        if (query.mLoadMagic)
            prepared << ' ' << result.mSpells.size() << " unique spells,"
                     << ' ' << result.mEnchantments.size() << " unique enchantments,"
                     << ' ' << result.mPotions.size() << " unique potions,"
                     << ' ' << result.mIngredients.size() << " unique ingredients,"
                     << ' ' << result.mMagicEffects.size() << " unique magic effects,"
                     << ' ' << result.mEnchantedItems.size() << " enchanted items,";
        if (query.mLoadLands)
            prepared << ' ' << result.mLands.size() << " unique lands,";
        if (query.mLoadStatics)
            prepared << ' ' << result.mStatics.size() << " unique statics,";

        Log(Debug::Info) << "Prepared" << prepared.str();

        return result;
    }
}
