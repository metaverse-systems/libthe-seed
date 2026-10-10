#include "PakAccess.hpp"

#include "BoundedBytes.hpp"

#include <new>
#include <stdexcept>

namespace seed::internal
{

void PakFailureThrow(const std::string &name, const std::string &file,
                     const std::vector<LoadError::Location> &locations, bool missing_is_not_found)
{
    try
    {
        throw;
    }
    catch(const LoadError &)
    {
        throw;
    }
    catch(const PakOpenError &error)
    {
        if(error.MissingGet() && missing_is_not_found)
        {
            throw LoadError(LoadError::Reason::NotFound, name, file, locations, error.what(), PakKind);
        }
        throw LoadError(LoadError::Reason::NotLoadable, name, file, locations, error.what(), PakKind);
    }
    catch(const PakDamaged &error)
    {
        throw LoadError(LoadError::Reason::NotLoadable, name, file, locations,
                        std::string("damaged: ") + error.what(), PakKind);
    }
    catch(const PakShortRead &error)
    {
        throw LoadError(LoadError::Reason::NotLoadable, name, file, locations,
                        std::string("damaged: ") + error.what(), PakKind);
    }
    catch(const PakReadError &error)
    {
        throw LoadError(LoadError::Reason::NotLoadable, name, file, locations, error.what(), PakKind);
    }
    catch(const PakNoMemory &error)
    {
        throw LoadError(LoadError::Reason::NotLoadable, name, file, locations, error.what(), PakKind);
    }
    catch(const std::bad_alloc &)
    {
        throw LoadError(LoadError::Reason::NotLoadable, name, file, locations,
                        "not enough memory to read the resource pak", PakKind);
    }
    catch(const std::length_error &)
    {
        throw LoadError(LoadError::Reason::NotLoadable, name, file, locations,
                        "not enough memory to read the resource pak", PakKind);
    }
    catch(const std::exception &error)
    {
        throw LoadError(LoadError::Reason::NotLoadable, name, file, locations,
                        std::string("internal error (") + detail::TypeName(error) + "): " + error.what(), PakKind);
    }
    catch(...)
    {
        throw LoadError(LoadError::Reason::NotLoadable, name, file, locations, "internal error (unknown exception)",
                        PakKind);
    }
}

void PakEntryRead(const PakFile &file, const PakEntry &entry, ecs::Resource &resource)
{
    try
    {
        resource.Data.resize(static_cast<std::size_t>(entry.size));
    }
    catch(const std::bad_alloc &)
    {
        throw PakNoMemory(entry.name, entry.size);
    }
    catch(const std::length_error &)
    {
        throw PakNoMemory(entry.name, entry.size);
    }
    if(entry.size > 0)
    {
        file.ReadAt(entry.offset, resource.Data.data(), entry.size);
    }
}

} // namespace seed::internal
