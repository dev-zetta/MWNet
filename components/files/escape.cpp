#include "escape.hpp"

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <sstream>

namespace Files
{
    const int escape_hash_filter::sEscape = '\x1c';
    const int escape_hash_filter::sHashIdentifier = 'h';
    const int escape_hash_filter::sEscapeIdentifier = 'e';

    escape_hash_filter::escape_hash_filter() : mSeenNonWhitespace(false), mFinishLine(false) {}
    escape_hash_filter::~escape_hash_filter() {}

    unescape_hash_filter::unescape_hash_filter() : expectingIdentifier(false) {}
    unescape_hash_filter::~unescape_hash_filter() {}

    std::string EscapeHashString::processString(const std::string & str)
    {
        std::istringstream stream(str);
        boost::iostreams::filtering_istream filteredStream;
        filteredStream.push(unescape_hash_filter());
        filteredStream.push(stream);
        std::ostringstream result;
        boost::iostreams::copy(filteredStream, result);
        return result.str();
    }

    EscapeHashString::EscapeHashString() : mData() {}

    EscapeHashString::EscapeHashString(const std::string & str) : mData(processString(str)) {}

    EscapeHashString::EscapeHashString(const std::string & str, size_t pos, size_t len)
        : mData(processString(str.substr(pos, len))) {}

    EscapeHashString::EscapeHashString(const char * s) : mData(processString(std::string(s))) {}

    EscapeHashString::EscapeHashString(const char * s, size_t n)
        : mData(processString(std::string(s, n))) {}

    EscapeHashString::EscapeHashString(size_t n, char c) : mData(n, c) {}

    std::string EscapeHashString::toStdString() const
    {
        return mData;
    }

    std::ostream & operator<< (std::ostream & os, const EscapeHashString & eHS)
    {
        return os << eHS.mData;
    }

    std::istream & operator>> (std::istream & is, EscapeHashString & eHS)
    {
        std::string str;
        is >> str;
        eHS = EscapeHashString(str);
        return is;
    }

    EscapeStringVector::EscapeStringVector() {}
    EscapeStringVector::~EscapeStringVector() {}

    std::vector<std::string> EscapeStringVector::toStdStringVector() const
    {
        std::vector<std::string> result;
        result.reserve(mVector.size());
        for (const auto & s : mVector)
            result.push_back(s.toStdString());
        return result;
    }

    void validate(boost::any &v, const std::vector<std::string> &tokens,
        Files::EscapeHashString * /*eHS*/, int /*a*/)
    {
        boost::program_options::validators::check_first_occurrence(v);
        const std::string & s = boost::program_options::validators::get_single_string(tokens);
        v = boost::any(Files::EscapeHashString(s));
    }

    void validate(boost::any &v, const std::vector<std::string> &tokens,
        EscapeStringVector * /*esv*/, int /*a*/)
    {
        if (v.empty())
            v = boost::any(EscapeStringVector());
        EscapeStringVector * esv = boost::any_cast<EscapeStringVector>(&v);
        for (const auto & token : tokens)
            esv->mVector.push_back(EscapeHashString(token));
    }

    std::istream & operator>> (std::istream & istream, EscapePath & escapePath)
    {
        std::string str;
        istream >> str;
        escapePath.mPath = boost::filesystem::path(EscapeHashString(str).toStdString());
        return istream;
    }

    PathContainer EscapePath::toPathContainer(const std::vector<EscapePath> & escapePathContainer)
    {
        PathContainer result;
        result.reserve(escapePathContainer.size());
        for (const auto & ep : escapePathContainer)
            result.push_back(std::filesystem::path(ep.mPath.string()));
        return result;
    }
}
