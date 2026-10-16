#include <libthe-seed/MachOParser.hpp>

#include "internal/BoundedBytes.hpp"
#include "internal/FileIO.hpp"
#include "internal/MachOLayout.hpp"
#include "internal/PagedFile.hpp"

#include <algorithm>
#include <stdexcept>

namespace the_seed
{

namespace {

using the_seed::internal::ByteSpan;
using the_seed::internal::ClassifyMachOMagic;
using the_seed::internal::DeclineText;
using the_seed::internal::GuardEntryPoint;
using the_seed::internal::MachOContainer;
using the_seed::internal::MachOKind;
using the_seed::internal::PagedFile;
using the_seed::internal::ParseMachOContainer;
using the_seed::internal::ReadFileBytes;
using the_seed::internal::SliceSupport;
using the_seed::internal::ThrowMalformed;

constexpr const char *kFormat = "Mach-O";

void RequireFourBytes(const std::vector<std::uint8_t> &bytes)
{
    if(bytes.size() < 4)
    {
        ThrowMalformed(kFormat, "File too small to be a Mach-O binary");
    }
}

} // anonymous namespace

MachOParser::Format MachOParser::DetectFormat(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&]() {
        // Only the first bytes decide the kind, so only they are read.
        const PagedFile file(file_path);
        switch(ClassifyMachOMagic(ByteSpan(file, kFormat)))
        {
        case MachOKind::Thin32Little:
        case MachOKind::Thin32Big:
            return Format::MachO32;
        case MachOKind::Thin64Little:
        case MachOKind::Thin64Big:
            return Format::MachO64;
        case MachOKind::Fat32:
        case MachOKind::Fat64:
            return Format::Fat;
        default:
            return Format::NotMachO;
        }
    });
}

bool MachOParser::IsMachO(const std::string &file_path)
{
    return DetectFormat(file_path) != Format::NotMachO;
}

bool MachOParser::IsFatBinary(const std::string &file_path)
{
    return DetectFormat(file_path) == Format::Fat;
}

std::vector<MachOParser::ArchSlice> MachOParser::GetArchSlices(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&]() {
        const auto bytes = ReadFileBytes(file_path);
        RequireFourBytes(bytes);

        const MachOContainer container = ParseMachOContainer(ByteSpan(bytes, kFormat));
        std::vector<ArchSlice> slices;
        slices.reserve(container.entries.size());
        for(const auto &entry : container.entries)
        {
            ArchSlice slice{};
            slice.cpu_type = entry.cputype;
            slice.cpu_subtype = entry.cpusubtype;
            slice.offset = entry.offset;
            slice.size = entry.size;
            slice.is_signed = entry.slice.codesig.has_value();
            if(entry.slice.codesig)
            {
                slice.signature_offset = entry.slice.codesig->dataoff;
                slice.signature_size = entry.slice.codesig->datasize;
            }
            slice.supported = entry.slice.support == SliceSupport::Supported;
            if(!slice.supported)
            {
                slice.unsupported_reason = DeclineText(entry.slice.support);
            }
            slices.push_back(std::move(slice));
        }
        return slices;
    });
}

std::vector<std::string> MachOParser::ListDependencies(const std::string &file_path)
{
    return GuardEntryPoint(kFormat, [&]() {
        const auto bytes = ReadFileBytes(file_path);
        RequireFourBytes(bytes);

        const MachOContainer container = ParseMachOContainer(ByteSpan(bytes, kFormat));
        std::vector<std::string> deps;
        for(std::size_t i = 0; i < container.entries.size(); ++i)
        {
            const auto &slice = container.entries[i].slice;
            if(slice.support != SliceSupport::Supported)
            {
                const auto &entry = container.entries[i];
                const std::string where = container.form == the_seed::internal::ContainerForm::Thin
                                              ? std::string()
                                              : "slice " + std::to_string(i) + " (" +
                                                    the_seed::internal::ArchName(entry.cputype) + "): ";
                throw std::runtime_error(file_path + ": " + where + DeclineText(slice.support));
            }
            for(const std::string &library : slice.dylibs)
            {
                if(std::find(deps.begin(), deps.end(), library) == deps.end())
                {
                    deps.push_back(library);
                }
            }
        }
        return deps;
    });
}
} // namespace the_seed
