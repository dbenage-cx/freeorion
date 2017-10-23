#ifndef VFS_NODE_H
#define VFS_NODE_H


#include "Common.hpp"
#include "../Export.h"
#include "../XMLDoc.h"
#include <boost/filesystem/path.hpp>
#include <map>
#include <unordered_map>
#include <unordered_set>


namespace VFS {
    namespace Node {
        class PathNode;

        /** @class Manager
        * 
        * Interface managing restrictions on filesystem operations
        */
        class FO_COMMON_API Manager {
        public:
            virtual ~Manager();

            /** Returns the singleton instance of this class, the free form GetVFSManager may be preferred */
            static Manager&                                     GetManager();

            /**
            */
            void                                                InitRootDir(const std::string& label,
                                                                            path_type path,
                                                                            bool allow_writes = false);

            /**
            */
            void                                                SetRootPath(const std::string& label,
                                                                            path_type path);

            /**
            */
            std::shared_ptr<PathNode> FO_COMMON_API             FindByPath(path_type path);

            /** Creates a new PathNode for a given path if one does not exist
            * 
            * @param[in] path Path to filesystem object
            * @return std::shared_ptr to created or previously existing object with path @p path */
            std::shared_ptr<PathNode> FO_COMMON_API             EmplacePath(path_type path);

            /** Iterates over a directory, creating instances for each filesystem object found
            * 
            * Captures file status provided by directory iteration for each contained object.
            * 
            * @param[in] dir_path Absolute path to directory for iteration.
            * @param[in] recursive_search If true, recurses into sub-directories.
            * @return vector List of filesystem objects found in @p dir_path, excludes <em>.</em> and <em>..</em> */
            std::vector<path_type> FO_COMMON_API                IterateDirectory(const path_type& dir_path,
                                                                                 bool recursive_search = true);

            /** Removes PathNode found for @p path */
            void FO_COMMON_API                                  Reset(const path_type& path);

            /** Removes all PathNode%s */
            void FO_COMMON_API                                  ResetAll();

        private:
            Manager();
            Manager(const Manager&) = delete;
            const Manager& operator=(const Manager&) = delete;

            class Impl;
            std::unique_ptr<Impl>                               m_impl;
            // FIXME: would prefer to use unique_ptr, but have not resolved invalid free on program exit
            //static std::unique_ptr<Manager>                             s_instance;
            static Manager*                                     s_instance;
        };

    }  // namespace Node
}  // namespace VFS

/** Helper function for VFS::Manager singleton */
FO_COMMON_API VFS::Node::Manager& GetVFSManager();

/** If @p lhs is lexigraphically contained by @p rhs.
 * 
 * Both @p lhs and @p rhs are compared by normalized absolute version.
 * If either was relative, absolute is resolved using current working directory. */
bool FO_COMMON_API                              PathContainedBy(const VFS::path_type& lhs,
                                                                const VFS::path_type& rhs);

/** If absolute path exists on the filesystem. */
bool FO_COMMON_API                              Exists(const VFS::path_type& abs_path);

/** If absolute path exists and is a directory. */
bool FO_COMMON_API                              IsDirectory(const VFS::path_type& abs_path);

/** If abolute path exists and is a regular file. */
bool FO_COMMON_API                              IsRegularFile(const VFS::path_type& abs_path);

/** If absolute path is empty.
 * 
 * If @p abs_path is a directory, it is considered empty if it contains an object other than . and ..
 * Otherwise @p abs_path is considered empty if the file size is 0, as determined by stat */
bool FO_COMMON_API                              IsEmpty(const VFS::path_type& abs_path);

/** Last modification time for absolute path */
std::time_t FO_COMMON_API                       LastWrite(const VFS::path_type& abs_path);

/** Return the portion of @p path after @p base_dir */
VFS::path_type FO_COMMON_API                    PathPortionFrom(const VFS::path_type& path,
                                                                const VFS::path_type& base_dir);

/** Return @p path with dot and dot-dot elements resolved */
VFS::path_type FO_COMMON_API                    NormalizedPath(const VFS::path_type& path);

/** All paths contained in a directory
 * 
 * @param[in] abs_dir_path Absolute path to directory
 * @param[in] recursive_search If true, recurses into sub-directories
 * @return vector List of filesytem objects found in @p abs_dir_path, excludes <em>.</em> and <em>..</em> */
std::vector<VFS::path_type> FO_COMMON_API       PathsInDir(const VFS::path_type& abs_dir_path,
                                                           bool recursive_search = true);

/** All paths contained in a directory filtered by a functor
 * 
 * @param[in] abs_dir_path Absolute path to directory
 * @param[in] pred Predicate functor accepting a VFS::path_type constant reference
 * @param[in] recursive_search If true, recurses into sub-directories
 * @return vector List of filesytem objects found in @p abs_dir_path which satisfy @p pred */
std::vector<VFS::path_type> FO_COMMON_API       PathsInDir(const VFS::path_type& abs_dir_path,
                                                           std::function<bool (const VFS::path_type&)> pred,
                                                           bool recursive_search);

/** All regular files contained in directory */
std::vector<VFS::path_type> FO_COMMON_API       FilesInDir(const VFS::path_type& dir_path,
                                                           bool recursive_search = true,
                                                           std::string extension = "");

/** Erases regular file from the filesystem.
 * 
 * @param[in] abs_path Absoulte path to regular file
 * @returns If @p abs_path was an existing regular file and was erased */
bool FO_COMMON_API                              EraseFile(const VFS::path_type& abs_path);

/** Read contents from file and return as string
 * 
 * @param[in] abs_path Absolute path to an existing regular file
 * @param[in] mode mode to open file
 * 
 * @return string Contents of @p abs_path */
std::string FO_COMMON_API                       ReadTextFile(const VFS::path_type& abs_path,
                                                             std::ios_base::openmode mode = std::ios_base::in);

/** Read contents from file into string
 * 
 * @param[in] abs_path Absolute path to an existing regular file
 * @param[in,out] contents std::string to read in contents of file
 * @param[in] mode mode to open file
 * 
 * @return bool If @p abs_path was successfully opened and read */
bool FO_COMMON_API                              ReadTextFile(const VFS::path_type& abs_path,
                                                             std::string& contents,
                                                             std::ios_base::openmode mode = std::ios_base::in);

/** Read contents from file through functor
 * 
 * @param[in] abs_path Absolute path to an existing regular file
 * @param[in,out] ifs_func Functor to stream contents to
 * @param[in] mode mode to open file
 * 
 * @return bool If @p abs_path was successfully opened and read, and @p ifs_func returns true */
bool FO_COMMON_API                              ReadFile(const VFS::path_type& abs_path,
                                                         std::function<bool (std::istream*)> ifs_func,
                                                         std::ios_base::openmode mode = std::ios_base::in);

/** Read contents from xml file into an XMLDoc
 * 
 * @param[in] abs_path Absolute path to an existing regular file
 * 
 * @return XMLDoc populated from calling ReadDoc with file contents */
XMLDoc FO_COMMON_API                            ReadXMLFile(const VFS::path_type& abs_path);

/** Write contents to file from string
 * 
 * @param[in] abs_path Absolute path to an existing regular file
 * @param[in,out] contents std::string file contents
 * @param[in] mode mode to open file
 * 
 * @return bool If @p abs_path was successfully opened and written */
bool FO_COMMON_API                              WriteTextFile(const VFS::path_type& abs_path,
                                                              const std::string& contents,
                                                              std::ios_base::openmode mode = std::ios_base::out);

/** Write contents to file through functor
 * 
 * @param[in] abs_path Absolute path to an existing regular file
 * @param[in,out] ofs_func Functor to stream contents from
 * @param[in] mode mode to open file
 * 
 * @return bool If @p abs_path was successfully opened and written, and @p ofs_func returns true */
bool FO_COMMON_API                              WriteFile(const VFS::path_type& abs_path,
                                                          std::function<bool (std::ostream*)> ofs_func,
                                                          std::ios_base::openmode mode = std::ios_base::out);

/** Create directory, creates any non-existant parent directories */
bool FO_COMMON_API                              CreateDirectories(const VFS::path_type& target_path);

#endif
