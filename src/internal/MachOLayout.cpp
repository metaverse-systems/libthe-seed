#include "MachOLayout.hpp"

#include "MachODefs.hpp"

#include <algorithm>
#include <cstdio>
#include <numeric>

namespace seed::internal
{

namespace
{

constexpr const char *kFormat = "Mach-O";

bool IsThin(MachOKind kind)
{
    return kind == MachOKind::Thin64Little || kind == MachOKind::Thin32Little ||
           kind == MachOKind::Thin64Big || kind == MachOKind::Thin32Big;
}

SliceSupport SupportFor(MachOKind kind)
{
    switch(kind)
    {
    case MachOKind::Thin64Little:
        return SliceSupport::Supported;
    case MachOKind::Thin32Little:
        return SliceSupport::ThirtyTwoBit;
    case MachOKind::Thin64Big:
        return SliceSupport::BigEndian;
    default:
        return SliceSupport::ThirtyTwoBitBigEndian;
    }
}

bool IsReferenceCommand(std::uint32_t cmd)
{
    return cmd == macho::kLcLoadDylib || cmd == macho::kLcLoadWeakDylib ||
           cmd == macho::kLcReexportDylib || cmd == macho::kLcLazyLoadDylib ||
           cmd == macho::kLcLoadUpwardDylib;
}

const char *ReferenceCommandName(std::uint32_t cmd)
{
    switch(cmd)
    {
    case macho::kLcLoadDylib:
        return "LC_LOAD_DYLIB";
    case macho::kLcLoadWeakDylib:
        return "LC_LOAD_WEAK_DYLIB";
    case macho::kLcReexportDylib:
        return "LC_REEXPORT_DYLIB";
    case macho::kLcLazyLoadDylib:
        return "LC_LAZY_LOAD_DYLIB";
    default:
        return "LC_LOAD_UPWARD_DYLIB";
    }
}

bool IsZeroFill(std::uint32_t section_flags)
{
    const std::uint32_t type = section_flags & macho::kSectionTypeMask;
    return type == macho::kSectionTypeZeroFill || type == macho::kSectionTypeGbZeroFill ||
           type == macho::kSectionTypeTlsZeroFill;
}

std::string SegmentName(const ByteSpan &command)
{
    std::string name;
    for(std::uint64_t i = 0; i < 16; ++i)
    {
        const char c = static_cast<char>(command.Read<std::uint8_t>(8 + i, ByteOrder::Little, "segment name"));
        if(c == '\0')
        {
            break;
        }
        name.push_back(c);
    }
    return name;
}

// The first four bytes of a candidate slice, which must be a thin Mach-O magic.
bool StartsWithThinMagic(const ByteSpan &file, std::uint64_t offset, std::uint64_t size)
{
    if(!RangeFits(offset, size, file.Size()) || size < 4)
    {
        return false;
    }
    return IsThin(ClassifyMachOMagic(file.Sub(offset, size, "slice")));
}

} // namespace

MachOKind ClassifyMachOMagic(const ByteSpan &file)
{
    if(file.Size() < 4)
    {
        return MachOKind::Unknown;
    }
    // Read one byte at a time so the host byte order plays no part.
    std::uint32_t bytes = 0;
    for(std::uint64_t i = 0; i < 4; ++i)
    {
        bytes = (bytes << 8) | file.Read<std::uint8_t>(i, ByteOrder::Little, "Mach-O magic");
    }
    switch(bytes)
    {
    case macho::kBytesThin64Little:
        return MachOKind::Thin64Little;
    case macho::kBytesThin32Little:
        return MachOKind::Thin32Little;
    case macho::kBytesThin64Big:
        return MachOKind::Thin64Big;
    case macho::kBytesThin32Big:
        return MachOKind::Thin32Big;
    case macho::kBytesFat32:
        return MachOKind::Fat32;
    case macho::kBytesFat64:
        return MachOKind::Fat64;
    default:
        return MachOKind::Unknown;
    }
}

std::string DeclineKind(SliceSupport support)
{
    switch(support)
    {
    case SliceSupport::BigEndian:
        return "big-endian";
    case SliceSupport::ThirtyTwoBit:
        return "32-bit";
    case SliceSupport::ThirtyTwoBitBigEndian:
        return "32-bit big-endian";
    default:
        return "";
    }
}

std::string DeclineText(SliceSupport support)
{
    return DeclineKind(support) +
           " Mac programs are not supported (supported: 64-bit little-endian arm64 and x86-64, "
           "alone or in a universal file)";
}

std::string ArchName(std::uint32_t cputype)
{
    if(cputype == 0x0100000C)
    {
        return "arm64";
    }
    if(cputype == 0x01000007)
    {
        return "x86_64";
    }
    char text[16];
    std::snprintf(text, sizeof(text), "cpu 0x%X", static_cast<unsigned>(cputype));
    return text;
}

SliceLayout ParseMachOSlice(const ByteSpan &file, std::uint64_t base, std::uint64_t size,
                            std::string_view scope)
{
    const ByteSpan view = (base == 0 && size == file.Size()) ? file : file.Sub(base, size, scope);

    const MachOKind kind = ClassifyMachOMagic(view);
    if(!IsThin(kind))
    {
        ThrowMalformed(kFormat, "Not a single-arch Mach-O binary");
    }
    (void)view.Sub(0, 12, "Mach-O header");

    SliceLayout layout;
    layout.base = base;
    layout.size = size;
    layout.support = SupportFor(kind);
    const bool big = kind == MachOKind::Thin64Big || kind == MachOKind::Thin32Big;
    const ByteOrder order = big ? ByteOrder::Big : ByteOrder::Little;
    layout.cputype = view.Read<std::uint32_t>(4, order, "cpu type");
    layout.cpusubtype = view.Read<std::uint32_t>(8, order, "cpu subtype");
    layout.first_content = size;
    if(layout.support != SliceSupport::Supported)
    {
        return layout;
    }

    const ByteOrder le = ByteOrder::Little;
    (void)view.Sub(0, macho::kHeader64Size, "Mach-O header");
    layout.filetype = view.Read<std::uint32_t>(12, le, "file type");
    layout.ncmds = view.Read<std::uint32_t>(16, le, "load command count");
    layout.sizeofcmds = view.Read<std::uint32_t>(20, le, "load command area size");
    layout.header_end = macho::kHeader64Size + static_cast<std::uint64_t>(layout.sizeofcmds);
    const ByteSpan area = view.Sub(macho::kHeader64Size, layout.sizeofcmds, "load commands");

    std::uint64_t cursor = 0; // within the load command area
    std::string segment_problem;
    for(std::uint32_t i = 0; i < layout.ncmds; ++i)
    {
        const std::string index = std::to_string(i);
        if(!RangeFits(cursor, macho::kLoadCommandHeaderSize, area.Size()))
        {
            ThrowMalformed(kFormat, "load command " + index +
                                        " does not fit in the load command area of " +
                                        std::to_string(area.Size()) + " bytes");
        }
        const auto cmd = area.Read<std::uint32_t>(cursor, le, "load command");
        const auto cmdsize = area.Read<std::uint32_t>(cursor + 4, le, "load command size");
        if(cmdsize < macho::kLoadCommandHeaderSize)
        {
            std::string message = "load command " + index + " size " + std::to_string(cmdsize) +
                                  " is smaller than 8";
            if(cmdsize == 0)
            {
                // A program signed again by an earlier version of the signer
                // could be left with an emptied signature command.
                message += "; this can be a program signed by an earlier version of the-seed; "
                           "rebuild it from the unsigned original";
            }
            ThrowMalformed(kFormat, message);
        }
        if(cmdsize % 4 != 0)
        {
            ThrowMalformed(kFormat, "load command " + index + " size " + std::to_string(cmdsize) +
                                        " is not a multiple of 4");
        }
        if(!RangeFits(cursor, cmdsize, area.Size()))
        {
            ThrowMalformed(kFormat, "load command " + index + " size " + std::to_string(cmdsize) +
                                        " runs past the end of the load commands");
        }
        const std::uint64_t command_offset = macho::kHeader64Size + cursor;
        layout.commands.push_back({command_offset, cmd, cmdsize});
        const ByteSpan command = area.Sub(cursor, cmdsize, "load command " + index);

        if(cmd == macho::kLcSegment64)
        {
            const std::string name = "LC_SEGMENT_64 command " + index;
            if(cmdsize < macho::kSegment64CommandSize)
            {
                ThrowMalformed(kFormat, name + " size " + std::to_string(cmdsize) +
                                            " is smaller than 72");
            }
            SegmentLayout segment;
            segment.command_offset = command_offset;
            segment.name = SegmentName(command);
            segment.vmaddr = command.Read<std::uint64_t>(24, le, "segment address");
            segment.vmsize = command.Read<std::uint64_t>(32, le, "segment size in memory");
            segment.fileoff = command.Read<std::uint64_t>(40, le, "segment file offset");
            segment.filesize = command.Read<std::uint64_t>(48, le, "segment file size");
            const auto nsects = command.Read<std::uint32_t>(64, le, "section count");
            if(!RangeFits(segment.fileoff, segment.filesize, size) && segment_problem.empty())
            {
                // Reported after the whole table is read, so that a problem
                // in a command that explains it (a signature that runs past
                // the end) is named first.
                segment_problem = name + " (" + segment.name + ") file range extends past the end of " +
                                  std::string(scope);
            }
            const std::uint64_t sections_end =
                macho::kSegment64CommandSize + static_cast<std::uint64_t>(nsects) * macho::kSection64Size;
            if(sections_end > cmdsize)
            {
                ThrowMalformed(kFormat, name + " has " + std::to_string(nsects) +
                                            " sections that do not fit in its " +
                                            std::to_string(cmdsize) + " bytes");
            }
            if(segment.name != "__TEXT" && segment.filesize > 0 && segment.fileoff > 0)
            {
                layout.first_content = std::min(layout.first_content, segment.fileoff);
            }
            for(std::uint32_t s = 0; s < nsects; ++s)
            {
                const std::uint64_t at = macho::kSegment64CommandSize + s * macho::kSection64Size;
                const auto section_offset = command.Read<std::uint32_t>(at + 48, le, "section file offset");
                const auto section_flags = command.Read<std::uint32_t>(at + 64, le, "section flags");
                if(section_offset != 0 && !IsZeroFill(section_flags))
                {
                    layout.first_content = std::min<std::uint64_t>(layout.first_content, section_offset);
                }
            }
            if(segment.name == "__TEXT" && !layout.text)
            {
                layout.text = segment;
            }
            else if(segment.name == "__LINKEDIT" && !layout.linkedit)
            {
                layout.linkedit = segment;
            }
            layout.segments.push_back(std::move(segment));
        }
        else if(cmd == macho::kLcCodeSignature)
        {
            const std::string name = "LC_CODE_SIGNATURE command " + index;
            if(cmdsize < macho::kCodeSignatureCommandSize)
            {
                ThrowMalformed(kFormat, name + " size " + std::to_string(cmdsize) +
                                            " is smaller than 16");
            }
            if(layout.codesig)
            {
                ThrowMalformed(kFormat, name + " is a second LC_CODE_SIGNATURE command");
            }
            CodeSignatureLayout sig;
            sig.command_offset = command_offset;
            sig.dataoff = command.Read<std::uint32_t>(8, le, "signature offset");
            sig.datasize = command.Read<std::uint32_t>(12, le, "signature size");
            if(!RangeFits(sig.dataoff, sig.datasize, size))
            {
                ThrowMalformed(kFormat, name + " data (offset " + std::to_string(sig.dataoff) +
                                            ", size " + std::to_string(sig.datasize) +
                                            ") extends past the end of " + std::string(scope));
            }
            if(sig.datasize != 0 && sig.dataoff < layout.header_end)
            {
                ThrowMalformed(kFormat, name + " data at offset " + std::to_string(sig.dataoff) +
                                            " is inside the load commands");
            }
            layout.codesig = sig;
        }
        else if(IsReferenceCommand(cmd))
        {
            const std::string name = std::string(ReferenceCommandName(cmd)) + " command " + index;
            if(cmdsize < macho::kDylibCommandMinSize)
            {
                ThrowMalformed(kFormat, name + " size " + std::to_string(cmdsize) +
                                            " is smaller than 24");
            }
            const auto name_offset = command.Read<std::uint32_t>(8, le, "dylib name offset");
            if(name_offset >= cmdsize)
            {
                ThrowMalformed(kFormat, name + " has its name at offset " + std::to_string(name_offset) +
                                            ", outside the command of " + std::to_string(cmdsize) +
                                            " bytes");
            }
            std::string library = command.CString(name_offset, cmdsize, name + " name");
            if(std::find(layout.dylibs.begin(), layout.dylibs.end(), library) == layout.dylibs.end())
            {
                layout.dylibs.push_back(std::move(library));
            }
        }

        cursor += cmdsize;
    }

    if(cursor != layout.sizeofcmds)
    {
        ThrowMalformed(kFormat, "the " + std::to_string(layout.ncmds) + " load commands use " +
                                    std::to_string(cursor) + " bytes but the header says " +
                                    std::to_string(layout.sizeofcmds));
    }
    if(!segment_problem.empty())
    {
        ThrowMalformed(kFormat, segment_problem);
    }
    return layout;
}

MachOContainer ParseMachOContainer(const ByteSpan &file)
{
    const MachOKind kind = ClassifyMachOMagic(file);
    MachOContainer container;

    if(IsThin(kind))
    {
        ContainerEntry entry;
        entry.size = file.Size();
        entry.slice = ParseMachOSlice(file, 0, file.Size());
        entry.cputype = entry.slice.cputype;
        entry.cpusubtype = entry.slice.cpusubtype;
        container.form = ContainerForm::Thin;
        container.entries.push_back(std::move(entry));
        return container;
    }
    if(kind != MachOKind::Fat32 && kind != MachOKind::Fat64)
    {
        ThrowMalformed(kFormat, "File is not a Mach-O binary");
    }

    const bool wide = kind == MachOKind::Fat64;
    const ByteOrder be = ByteOrder::Big;
    container.form = wide ? ContainerForm::Fat64 : ContainerForm::Fat32;
    if(file.Size() < macho::kFatHeaderSize)
    {
        ThrowMalformed(kFormat, "Truncated fat header");
    }
    const std::uint64_t entry_size = wide ? macho::kFatArch64Size : macho::kFatArch32Size;
    const std::uint64_t count = file.Read<std::uint32_t>(4, be, "fat_arch count");
    // A Java class file starts with the same four bytes; its next four are a
    // version number, never a plausible slice count of a Mach-O file.
    if(count == 0 || count > kMaxFatArchCount)
    {
        ThrowMalformed(kFormat, "File is not a Mach-O binary");
    }
    const std::uint64_t table_end = macho::kFatHeaderSize + count * entry_size;
    if(table_end > file.Size())
    {
        ThrowMalformed(kFormat, "Truncated fat_arch entry table: " + std::to_string(count) +
                                    " entries need " + std::to_string(table_end) +
                                    " bytes but the file has " + std::to_string(file.Size()));
    }

    for(std::uint64_t i = 0; i < count; ++i)
    {
        const std::uint64_t at = macho::kFatHeaderSize + i * entry_size;
        ContainerEntry entry;
        entry.cputype = file.Read<std::uint32_t>(at, be, "fat_arch cpu type");
        entry.cpusubtype = file.Read<std::uint32_t>(at + 4, be, "fat_arch cpu subtype");
        if(wide)
        {
            entry.offset = file.Read<std::uint64_t>(at + 8, be, "fat_arch offset");
            entry.size = file.Read<std::uint64_t>(at + 16, be, "fat_arch size");
            entry.align = file.Read<std::uint32_t>(at + 24, be, "fat_arch alignment");
        }
        else
        {
            entry.offset = file.Read<std::uint32_t>(at + 8, be, "fat_arch offset");
            entry.size = file.Read<std::uint32_t>(at + 12, be, "fat_arch size");
            entry.align = file.Read<std::uint32_t>(at + 16, be, "fat_arch alignment");
        }
        if(i == 0 && !StartsWithThinMagic(file, entry.offset, entry.size))
        {
            ThrowMalformed(kFormat, "File is not a Mach-O binary");
        }
        const std::string name = "fat slice " + std::to_string(i);
        (void)file.Sub(entry.offset, entry.size, name);
        if(entry.offset < table_end)
        {
            ThrowMalformed(kFormat, name + " starts at offset " + std::to_string(entry.offset) +
                                        ", inside the fat header table that ends at " +
                                        std::to_string(table_end));
        }
        container.entries.push_back(std::move(entry));
    }

    // No two slices may share bytes.
    std::vector<std::size_t> by_offset(container.entries.size());
    std::iota(by_offset.begin(), by_offset.end(), std::size_t{0});
    std::stable_sort(by_offset.begin(), by_offset.end(), [&](std::size_t a, std::size_t b) {
        return container.entries[a].offset < container.entries[b].offset;
    });
    for(std::size_t k = 1; k < by_offset.size(); ++k)
    {
        const auto &previous = container.entries[by_offset[k - 1]];
        const auto &current = container.entries[by_offset[k]];
        if(previous.offset + previous.size > current.offset)
        {
            ThrowMalformed(kFormat, "fat slice " + std::to_string(by_offset[k - 1]) + " and fat slice " +
                                        std::to_string(by_offset[k]) + " overlap");
        }
    }

    for(std::size_t i = 0; i < container.entries.size(); ++i)
    {
        ContainerEntry &entry = container.entries[i];
        entry.slice = ParseMachOSlice(file, entry.offset, entry.size, "fat slice " + std::to_string(i));
    }
    return container;
}

} // namespace seed::internal
