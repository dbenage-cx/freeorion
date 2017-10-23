#ifndef VFS_COMMON_H
#define VFS_COMMON_H

#include "../Logger.h"
#include <boost/filesystem/path.hpp>


namespace {
        DeclareThreadSafeLogger(vfs);
}

namespace VFS {
    using path_type = boost::filesystem::path;

    static const path_type                                  EMPTY_PATH;
}

#endif
