#ifndef VFS_CONTENT_H
#define VFS_CONTENT_H

#include "Common.hpp"
#include "../Export.h"

#include <string>


namespace VFS {
    namespace Content {
        class FO_COMMON_API Manager {
        public:
            static Manager&                                     InitOrGet(const path_type& search_dir);

            /**
            */
            std::vector<std::string>                            SearchDirs() const;

            void                                                AddSearchDir(const path_type& path);

            path_type                                           GetPath(const path_type& relative_path);

        private:
            Manager(const path_type& search_dir);
            Manager(const Manager&) = delete;
            const Manager& operator=(const Manager&) = delete;

            class Impl;
            std::unique_ptr<Impl>                               m_impl;

            static bool                                         s_initialized;
            static Manager*                                     s_instance;
        };
    }
}


FO_COMMON_API void InitContentManager(const VFS::path_type& search_dir);

FO_COMMON_API VFS::Content::Manager& GetContentManager();

#endif
