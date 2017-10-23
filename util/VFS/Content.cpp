#include "Content.h"

#include "Node.h"
#include "ContentDir.hpp"
#include "../../parse/Parse.h"

#include <boost/filesystem/path.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>

#include <set>
#include <unordered_set>
#include <numeric>


namespace VFS{
    namespace Content {
        //      ##      DirPathCache         ##

        class DirPathCache {
        public:
            DirPathCache() = default;
            virtual ~DirPathCache();

            path_type                                           GetPath(const path_type& relative_path);

            bool                                                InsertPath(const path_type& relative_path,
                                                                        const path_type& absolute_path);

            void                                                InsertOrAssignPath(const path_type& relative_path,
                                                                                   const path_type& absolute_path);

            void                                                Clear()
            { m_paths.clear(); }
        private:
            std::map<path_type, path_type>                      m_paths;
        };

        DirPathCache::~DirPathCache() = default;

        path_type DirPathCache::GetPath(const path_type& relative_path) {
            auto it = m_paths.find(relative_path);
            if (it != m_paths.end())
                return it->second;

            WarnLogger(vfs) << "Path not found " << relative_path.string();
            return EMPTY_PATH;
        }

        bool DirPathCache::InsertPath(const path_type& relative_path, const path_type& absolute_path) {
            auto it = m_paths.find(relative_path);
            if (it != m_paths.end())
                return false;

            m_paths.emplace(relative_path, absolute_path);
            return true;
        }

        void DirPathCache::InsertOrAssignPath(const path_type& relative_path, const path_type& absolute_path) {
            auto it = m_paths.find(relative_path);
            if (it != m_paths.end())
                it->second = absolute_path;
            else
                m_paths.emplace(relative_path, absolute_path);
        }


        //      ##      DirSet         ##

        namespace tag {
            struct id {};
            struct label {};
            struct depth {};
        }


        class DirSet {
        public:
            DirSet() :
                m_cache(std::unique_ptr<DirPathCache>(new DirPathCache()))
            {}

            virtual ~DirSet();

            /**
             */
            std::vector<std::string>                            AllDirs() const;

            std::vector<std::string>                            GetDirs(bool enabled) const;
            /**
            */
            path_type                                           GetPath(const path_type& relative_path) const;

            /**
             */
            void                                                AddDir(Dir::ptr_type dir);

            /**
            */
            void                                                EnableDir(const std::string& dir_label);

            /**
            */
            void                                                DisableDir(const std::string& dir_label);

            void                                                RefreshCache() const;
        private:
            using id_idx                =   boost::multi_index::hashed_unique<
                                                boost::multi_index::tag<tag::id>,
                                                boost::multi_index::identity<Dir::ptr_type>>;

            using label_idx             =   boost::multi_index::ordered_unique<
                                                boost::multi_index::tag<tag::label>,
                                                boost::multi_index::const_mem_fun<
                                                    Dir,
                                                    std::string,
                                                    &Dir::ptr_type::element_type::LabelKey>>;

            using depth_idx             =   boost::multi_index::ordered_non_unique<
                                                boost::multi_index::tag<tag::depth>,
                                                boost::multi_index::const_mem_fun<
                                                    Dir,
                                                    std::size_t,
                                                    &Dir::ptr_type::element_type::ReqDepth>>;

            using container_idx         =   boost::multi_index_container<
                                                Dir::ptr_type,
                                                boost::multi_index::indexed_by<
                                                    id_idx,
                                                    label_idx,
                                                    depth_idx>>;

            DirSet(const DirSet&) = delete;
            const DirSet& operator=(const DirSet&) = delete;

            bool                                                ValidateDir(const std::string& dir_label) const;

            void                                                ValidateDirs() const;

            container_idx                                       m_dirs;
            // TODO move cache to other?
            mutable bool                                        m_requires_refresh = false;
            /** map of relative paths to resolved absolute paths for enabled Dir%s */
            std::unique_ptr<DirPathCache>                       m_cache;
        };

        DirSet::~DirSet() = default;

        std::vector<std::string> DirSet::AllDirs() const {
            std::vector<std::string> retval;
            auto& dirs_by_depth = m_dirs.get<tag::depth>();
            for (const auto& dir : dirs_by_depth)
                retval.push_back(dir->LabelKey());
            return retval;
        }

        std::vector<std::string> DirSet::GetDirs(bool enabled) const {
            std::vector<std::string> retval;

            ValidateDirs();

            auto& dirs_by_depth = m_dirs.get<tag::depth>();
            for (const auto& dir : dirs_by_depth) {
                if (dir->Enabled() == enabled)
                    retval.push_back(dir->LabelKey());
            }

            return retval;
        }

        path_type DirSet::GetPath(const path_type& relative_path) const {
            if (m_dirs.empty()) {
                ErrorLogger(vfs) << "No content dirs";
                return EMPTY_PATH;
            }

            RefreshCache();
            return m_cache->GetPath(relative_path);
        }

        void DirSet::AddDir(Dir::ptr_type dir) {
            m_dirs.insert(std::move(dir));
        }

        void DirSet::EnableDir(const std::string& dir_label) {
            auto& dirs_by_label = m_dirs.get<tag::label>();
            auto it = dirs_by_label.find(dir_label);
            if (it != dirs_by_label.end()) {
                if (!ValidateDir((*it)->LabelKey())) {
                    WarnLogger(vfs) << "Dir " << dir_label << " did not validate";
                    return;
                }

                if ((*it)->SetEnabled())
                    m_requires_refresh = true;
            } else {
                ErrorLogger(vfs) << "No directory found for label " << dir_label;
            }
        }

        void DirSet::DisableDir(const std::string& dir_label) {
            auto& dirs_by_label = m_dirs.get<tag::label>();
            auto it = dirs_by_label.find(dir_label);
            if (it != dirs_by_label.end()) {
                if ((*it)->SetEnabled(false))
                    m_requires_refresh = true;
            } else {
                ErrorLogger(vfs) << "No directory found for label " << dir_label;
            }
        }

        void DirSet::RefreshCache() const {
            if (!m_requires_refresh)
                return;
            m_requires_refresh = false;

            ValidateDirs();

            m_cache->Clear();
            auto& dirs_by_depth = m_dirs.get<tag::depth>();
            for (auto dir_it = dirs_by_depth.rbegin(); dir_it != dirs_by_depth.rend(); ++dir_it) {
                if (!(*dir_it)->Enabled())
                    continue;

                for (const auto& path : PathsInDir((*dir_it)->Path())) {
                    auto relative_path = PathPortionFrom(path, (*dir_it)->Path());
                    if ((*dir_it)->IsExplicit(relative_path)) {
                        m_cache->InsertOrAssignPath(relative_path, path);
                    } else {
                        // Insert path if not previously added
                        m_cache->InsertPath(relative_path, path);
                    }
                }
            }
        }

        bool DirSet::ValidateDir(const std::string& dir_label) const {
            //auto& dirs_by_depth = m_dirs.get<tag::depth>();
            auto& dirs_by_label = m_dirs.get<tag::label>();
            std::size_t depth = 0;
            auto dir_it = dirs_by_label.find(dir_label);
            if (dir_it == dirs_by_label.end())
                return false;

            for (const auto& req : (*dir_it)->Requires()) {
                auto req_it = dirs_by_label.find(req.first);
                if (req_it == dirs_by_label.end()) {
                    ErrorLogger(vfs) << "Dir " << dir_label << " missing requirement " << req.first;
                    return false;
                }

                depth = std::max(depth, (*req_it)->ReqDepth() + 1);
                if (!(*req_it)->Enabled()) {
                    if ((*dir_it)->SetEnabled(false))
                        ErrorLogger(vfs) << "Enabled dir " << dir_label << " has disabled requirement " << req.first;
                    return false;
                }
            }

            (*dir_it)->SetDepth(depth);
            return true;
        }

        void DirSet::ValidateDirs() const {
            auto& dirs_by_depth = m_dirs.get<tag::depth>();
            auto& dirs_by_label = m_dirs.get<tag::label>();
            for (auto it = dirs_by_depth.rbegin(); it != dirs_by_depth.rend(); ++it) {
                if (!(*it)->Enabled())
                    continue;

                for (const auto& req : (*it)->Requires()) {
                    auto req_it = dirs_by_label.find(req.first);
                    if (req_it != dirs_by_label.end() && (*req_it)->Enabled())
                        continue;

                    ErrorLogger(vfs) << "Dir " << (*it)->LabelKey() << " missing or disabled requirement " << req.first;
                    (*it)->SetEnabled(false);
                    break;
                }
            }
        }


        //      ##      Manager::Impl         ##

        class Manager::Impl {
        public:
            Impl(const path_type& search_dir); // FIXME figure out std::initializer_list
            virtual ~Impl();

            /**
            */
            std::vector<std::string>                            SearchDirs() const;

            void                                                AddSearchDir(const path_type& path);

            path_type                                           GetPath(const path_type& relative_path);
        private:
            Impl(const Impl&) = delete;
            const Impl& operator=(const Impl&) = delete;

            std::unique_ptr<DirSet>                             m_dir_set;
            std::set<path_type>                                 m_search_paths;
        };

        Manager::Impl::Impl(const path_type& search_dir) {
            AddSearchDir(search_dir);
        }

        Manager::Impl::~Impl() = default;

        void Manager::Impl::AddSearchDir(const path_type& path) {
            if (!m_search_paths.emplace(path).second)
                return;

            auto definition_ifs_func = [](const path_type& file) {
                    return IsRegularFile(file) && file.filename() == Dir::DEFINITION_FILENAME;
                };

            for (const auto& def_file : PathsInDir(path, definition_ifs_func, true)) {
                m_dir_set->AddDir(parse::content_dir(def_file));
            }
        }

        path_type Manager::Impl::GetPath(const path_type& relative_path) {
            return m_dir_set->GetPath(relative_path);
        }


        //      ##      Manager         ##

        // FIXME std::unique_ptr<Manager> Manager::s_instance = nullptr;
        Manager* Manager::s_instance = nullptr;

        Manager::Manager(const path_type& search_dir) {
            if (s_instance)
                throw std::runtime_error("Attempted to create more than one VFS::Content::Manager");
            // FIXME s_instance.reset(this);
            s_instance = this;
            m_impl.reset(new Impl(search_dir));
            TraceLogger(vfs) << "Created VFS Content Manager";
        }

        Manager& Manager::InitOrGet(const path_type& search_dir) {
            if (!s_initialized && search_dir == EMPTY_PATH)
                throw "VFS Contnent Manager not initialized";
            else if (s_initialized && search_dir != EMPTY_PATH)
                ErrorLogger(vfs) << "VFS Content Manager previously initialized";

            static Manager manager(search_dir);
            if (!s_initialized)
                s_initialized = true;
            return manager;
        }

        std::vector<std::string> Manager::SearchDirs() const {
            return m_impl->SearchDirs();
        }
        void Manager::AddSearchDir(const path_type& path) {
            m_impl->AddSearchDir(path);
        }

        path_type Manager::GetPath(const path_type& path) {
            return m_impl->GetPath(path);
        }
    }  // namespace Content
}  // namespace VFS


void InitContentManager(const VFS::path_type& search_dir) {
    VFS::Content::Manager::InitOrGet(search_dir); //fu
}

VFS::Content::Manager& GetContentManager() {
    return VFS::Content::Manager::InitOrGet(VFS::EMPTY_PATH);
}


