#ifndef OPENMW_BASEPACKETPROCESSOR_HPP
#define OPENMW_BASEPACKETPROCESSOR_HPP

#include <string>
#include <memory>
#include <unordered_map>
#include <stdexcept>

#define BPP_INIT(packet_id) packetID = packet_id; strPacketID = #packet_id; className = typeid(this).name(); avoidReading = false;

template<typename Proccessor>
class BasePacketProcessor
{
public:
    typedef std::unordered_map<unsigned char, std::unique_ptr<Proccessor>> processors_t;
    unsigned char GetPacketID()
    {
        return packetID;
    }
    std::string GetNameOfID()
    {
        return strPacketID;
    }

    std::string GetClassName()
    {
        return className;
    }

    static void AddProcessor(std::unique_ptr<Proccessor> processor)
    {
        if (!processor)
            throw std::invalid_argument("cannot register a null packet processor");

        const unsigned char packetId = processor->GetPacketID();
        const auto existing = processors.find(packetId);
        if (existing != processors.end())
            throw std::logic_error("processor " + existing->second->strPacketID
                + " already registered. Check " + processor->className + " and "
                + existing->second->className);

        processors.emplace(packetId, std::move(processor));
    }

    static void AddProcessor(Proccessor *processor)
    {
        AddProcessor(std::unique_ptr<Proccessor>(processor));
    }
protected:
    unsigned char packetID = 0;
    std::string strPacketID;
    std::string className;
    bool avoidReading = false;
    static processors_t processors;
};

#endif //OPENMW_BASEPACKETPROCESSOR_HPP
