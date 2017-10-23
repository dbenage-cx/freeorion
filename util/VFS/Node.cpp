#include "Node.h"

#include "../Directories.h"

#include <bitset>
#include <unordered_set>
#include <chrono>
#include <iostream>
#include <mutex>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/timer/timer.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>


// TODO RootNode - determine if we should require label specifier (may not want to automatically replace some DirNodes)
// TODO audit backtracking (dot_dot) and relative paths
// TODO PathNode - lock/dirty flag on pending path change? (Path(), String(), ...)
namespace {
    using fs_error_type = boost::filesystem::filesystem_error;

    /** Common message layout for error logs generated after catching fs_error_type */
    std::string CommonFSErrorString(const VFS::path_type& path, const fs_error_type& ec) {
        return "Filesystem error during access of " + PathToString(path) + "\n\t" + ec.what();
    }

    enum class ElementType : int {
        RELATIVE = 0,
        INVALID,
        DOT,
        DOT_DOT,
        SEPERATOR,
        SINGLE,
        MULTIPLE  // e.g. a normal path
    };

    bool operator==(const VFS::path_type& lhs, ElementType rhs) {
        auto lhs_str = PathToString(lhs);
        switch (rhs) {
            case ElementType::RELATIVE :
                return lhs_str == "*?";
            case ElementType::INVALID :
                return lhs_str == ".....";
            case ElementType::DOT :
                return lhs_str == ".";
            case ElementType::DOT_DOT :
                return lhs_str == "..";
            case ElementType::SEPERATOR :
                return lhs_str == "/";
            case ElementType::SINGLE :
                return !lhs.has_parent_path() || lhs.parent_path() == lhs;
            case ElementType::MULTIPLE :
                return lhs.parent_path() != lhs;
            default:
                return false;
        }
    }

    bool operator!=(const VFS::path_type& lhs, ElementType rhs)
    { return !(lhs == rhs); }

    VFS::path_type PathFrom(ElementType rhs) {
        switch (rhs) {
            case ElementType::RELATIVE :
                return VFS::path_type { "*?" };
                break;
            case ElementType::INVALID :
                return VFS::path_type { "....." };
                break;
            case ElementType::DOT :
                return VFS::path_type { "." };
                break;
            case ElementType::DOT_DOT :
                return VFS::path_type { ".." };
                break;
            case ElementType::SEPERATOR :
                return VFS::path_type { "/" };
                break;
            case ElementType::SINGLE :
            case ElementType::MULTIPLE :
            default:
                return VFS::EMPTY_PATH;
        }
    }
/*
    ElementType ElementTypeFrom(const VFS::path_type& rhs) {
        auto rhs_fn { rhs.filename() };
        // first case also matches for empty path argument
        if (rhs_fn == ElementType::INVALID)
            return ElementType::INVALID;
        else if (rhs_fn == ElementType::RELATIVE)
            return ElementType::RELATIVE;
        else if (rhs_fn == ElementType::DOT)
            return ElementType::DOT;
        else if (rhs_fn == ElementType::DOT_DOT)
            return ElementType::DOT_DOT;
        else if (rhs_fn == ElementType::SEPERATOR)
            return ElementType::SEPERATOR;
        else if (!rhs.has_parent_path() || rhs.parent_path() == rhs)
            return ElementType::SINGLE;
        else if (rhs.parent_path() != rhs)
            return ElementType::MULTIPLE;

        return ElementType::INVALID;
    }
*/

    std::vector<VFS::path_type> ElementsFromPath(const VFS::path_type& path) {
        std::vector<VFS::path_type> retval;
        int prev_sep = 0;
        // FIXME windows root
        for (auto node : path) {
            if (node == ElementType::SEPERATOR) {
                ++prev_sep;
            } else if (prev_sep > 0) {  // resolve multiple separators as a single seperator for next node
                retval.push_back(std::string(prev_sep, '/') + node.string());
                prev_sep = 0;
            } else {
                retval.push_back(node);
            }
        }

        return retval;
    }

    VFS::path_type NormalizePathFrom(const VFS::path_type& path) {
        if (path == VFS::EMPTY_PATH) {
            WarnLogger(vfs) << "Attempt to normalize empty path...";
            return path;
        }

        static std::map<VFS::path_type, VFS::path_type> normalized_cache {};
        auto cache_it = normalized_cache.find(path);
        if (cache_it != normalized_cache.end())
            return cache_it->second;

        int prev_dot_dot = 0;
        VFS::path_type result{};
        for (auto node = path.rbegin(); node != path.rend(); ++node) {
            auto fn = node->filename();
            if (fn == ElementType::DOT_DOT)
                ++prev_dot_dot;
            else if (fn == ElementType::RELATIVE)
                continue;
            else if (prev_dot_dot > 0)
                --prev_dot_dot;
            else if (fn != ElementType::DOT && fn != VFS::EMPTY_PATH)
                result = node->filename() / result;
            // else continue
        }

        normalized_cache[path] = result;

        return result;
    }

    /** Retrieve PathNode with key using string form of @p path, a new PathNode is created if not existing. */
    std::shared_ptr<VFS::Node::PathNode> GetPathNode(VFS::path_type path) {
        auto normal_path = NormalizePathFrom(path);
        auto& mgr = VFS::Node::Manager::GetManager();
        auto retval = mgr.FindByPath(normal_path);
        if (!retval)
            retval = mgr.EmplacePath(normal_path);
        return retval;
    }

}  // anonymous namespace


namespace VFS {
    namespace Node {
        /** @class PathNode
        * 
        * Manages restrictions and path resolution for a specific path element */
        class PathNode {
        public:
            /**                                                     @name Types */ /** @{ */
            /** Represents a single element within a path (a, b, or c.h in a/b/c.h) */
            using value_type = boost::filesystem::path;
            using ptr_type = std::shared_ptr<PathNode>;
            using status_type = boost::filesystem::file_status;

            /** @} */ /**                                           @name Statics */ /** @{ */

            /** Delay required between repeated stat calls per PathNode (approximate wall clock) */
    //        static constexpr boost::timer::nanosecond_type  STATUS_TIMEOUT { 2 * 10000000LL };  // 200 ms
            static const status_type                                DEFAULT_STATUS;

            /** @} */ /**                                           @name Ctors/Dtors */ /** @{ */

            PathNode(const value_type& path_element, ptr_type parent, bool allow_writes) :
                m_path_element(path_element),
                m_parent_node(parent),
                m_allow_writes(allow_writes)
            {
    //            m_status_timer.stop();
                assert(path_element == ElementType::SINGLE || path_element == ElementType::MULTIPLE);
                std::cout << "Ctor " << (parent ? parent->Path().string() : std::string("(root)"))
                        << " / " << m_path_element.string() << " : " << String() << std::endl;
            }

            virtual ~PathNode();

            /** @} */ /**                                           @name Accessors */ /** @{ */

            /** If this PathNode is considered a root filesystem object */
            bool                                                        IsRoot() const
            { return !m_parent_node; }

            /** Returns the parent PathNode for this PathNode */
            ptr_type                                                    Parent() const
            { return m_parent_node; }

            /** Returns the path resolution to this PathNode (implicitly normalized and absolute) */
            path_type                                                   Path() const;

            std::string                                                 String() const
            { return PathToString(Path()); }

            /** If this node is, or stems from, a relative root path */
            bool                                                        IsRelative() const;

            ptr_type                                                    Root() const;

            /** If this PathNode or a parent PathNode has the parent @p parent_node */
            bool                                                        HasAncessor(ptr_type parent_node) const;

            /** If the path for this PathNode resolves to @p path or is contained by @p path
            * @param[in] path Normalized path */
            bool                                                        IsOrContainedBy(const path_type& path) const;

            path_type                                                   PathPortionFrom(const path_type& path) const;

            /** fs_status_type of the underlying resolved path to this PathNode */
            status_type                                                 Status() const;

            /** If this PathNode currently exists on the filesystem */
            bool                                                        Exists() const
            {
                if (IsRelative()) {
                    DebugLogger(vfs) << "exists ? no - relative path " << String();
                    return false;
                }
                return boost::filesystem::exists(Status());
                //return !IsRelative() && boost::filesystem::exists(Status());
            }

            /** @} */ /**                                           @name Mutators */ /** @{ */

            /** If this PathNode or a parent PathNode allows writes (Note: this is separate from filesystem/OS permission) */
            bool                                                        Writeable();

            void                                                        SetWriteable(bool allow_writes);

            void                                                        OverrideParent(ptr_type new_parent)
            { m_parent_node = new_parent; }
        protected:
            void                                                        OverridePathElement(const VFS::path_type& path)
            { m_path_element = path; }

            /** @} */

        private:
            value_type                                                  m_path_element;
            ptr_type                                                    m_parent_node;
            bool                                                        m_allow_writes;
            mutable status_type                                         m_status_cache = DEFAULT_STATUS;
    //        mutable boost::timer::cpu_timer                             m_status_timer = {};
        };

        PathNode::~PathNode() = default;

        const PathNode::status_type PathNode::DEFAULT_STATUS {};

        path_type PathNode::Path() const {
            auto retval = m_parent_node ? m_parent_node->Path() : EMPTY_PATH;
            if (m_path_element != EMPTY_PATH && m_path_element != ElementType::RELATIVE)
                retval /= m_path_element;
            return retval;
        }

        bool PathNode::IsRelative() const {
            return (!m_parent_node && Path() == ElementType::RELATIVE) ||
                    (m_parent_node && m_parent_node->IsRelative());
        }

        PathNode::ptr_type PathNode::Root() const {
            PathNode::ptr_type retval;
            if (IsRelative())
                return retval;

            if (m_parent_node) {
                retval = m_parent_node->Root();
                if (!retval)
                    retval = m_parent_node;
            }

            if (!retval)
                retval = std::make_shared<PathNode>(*this);

            return retval;
        }

        bool PathNode::HasAncessor(ptr_type parent_node) const {
            return parent_node && m_parent_node &&
                    (m_parent_node->Path() == parent_node->Path() || m_parent_node->HasAncessor(parent_node));
        }

        bool PathNode::IsOrContainedBy(const path_type& path) const {
            if (path == EMPTY_PATH)
                return IsRoot();
            return path == Path() || (m_parent_node && m_parent_node->IsOrContainedBy(path));
        }

        bool PathNode::Writeable() {
            if (!m_allow_writes && m_parent_node && m_parent_node->Writeable())
                m_allow_writes = true;
            return m_allow_writes;
        }

        void PathNode::SetWriteable(bool allow_writes)
        { m_allow_writes = (allow_writes || (m_parent_node && m_parent_node->Writeable())); }

        PathNode::status_type PathNode::Status() const {
    //        if (!m_status_timer.is_stopped() && m_status_timer.elapsed().wall < STATUS_TIMEOUT) {
    //            DebugLogger(vfs) << "return status cache for " << String();
    //            return m_status_cache;
    //        }

    //        m_status_timer.stop();
    //        m_status_timer = boost::timer::cpu_timer();
    //        m_status_cache = DEFAULT_STATUS;

            if (IsRelative())
                return m_status_cache;

            try {
                auto path = Path();
                m_status_cache = boost::filesystem::status(path);
            } catch (const fs_error_type& ec) {
                ErrorLogger(vfs) << CommonFSErrorString(Path(), ec);
            }
            return m_status_cache;
        }

        path_type PathNode::PathPortionFrom(const path_type& path) const {
            auto retval = EMPTY_PATH;

            auto current_path = Path();
            if (current_path == path)
                return retval;

            if (m_path_element == ElementType::RELATIVE) {
                retval = PathFrom(ElementType::DOT_DOT);
            } else if (m_parent_node) {
                retval = m_parent_node->PathPortionFrom(path);
                if (retval == ElementType::INVALID)
                    return retval;

                retval /= m_path_element;
            } else {
                retval = PathFrom(ElementType::INVALID);
            }

            return retval;
        }


        /** @class DirNode
        * 
        * A potential directory on the filesystem */
        class DirNode : public PathNode {
        public:
            using ptr_type = std::shared_ptr<DirNode>;

            DirNode(const path_type& path, PathNode::ptr_type parent, bool allow_writes) :
                    PathNode(path, parent, allow_writes || (parent && parent->Writeable()))
            {}

            DirNode(const path_type& path, PathNode::ptr_type parent) :
                    DirNode(path, parent, false)
            {}

            ~DirNode();

        private:
            DirNode() = delete;
            DirNode(const DirNode&) = delete;
            const DirNode& operator=(const DirNode&) = delete;
        };

        DirNode::~DirNode() = default;


        /** @class RootNode
        * 
        * Defines an absolute path which may be altered */
        class RootNode : public DirNode {
        public:
            using value_type = path_type;
            using ptr_type =  std::shared_ptr<RootNode>;
            using label_type = std::string;

            RootNode(const label_type& label, const path_type& path, bool allow_writes) :
                    DirNode(path, nullptr, allow_writes),
                m_label(label)
            {}

            ~RootNode();

            const label_type&                                           Label() const
            { return m_label; }

            void                                                        SetPath(const value_type& path);
        private:
            RootNode() = delete;
            RootNode(const RootNode&) = delete;
            const RootNode& operator=(const RootNode&) = delete;

            label_type                                                  m_label;
        };

        RootNode::~RootNode() = default;

        void RootNode::SetPath(const value_type& path) {
            if (path == EMPTY_PATH)
                return;
            OverridePathElement(path);
        }


        /** @class FileNode
        * 
        * A potential regular file on the filesystem. */
        class FileNode : public PathNode {
        public:
            using ptr_type = std::shared_ptr<FileNode>;

            FileNode(const path_type& filename, PathNode::ptr_type parent, bool allow_writes) :
                    PathNode(filename, parent, allow_writes || (parent && parent->Writeable()))
            {}

            FileNode(const path_type& filename, PathNode::ptr_type parent) :
                    FileNode(filename, parent, false)
            {}

            virtual ~FileNode();

        private:
            FileNode() = delete;
            FileNode(const FileNode&) = delete;
            const FileNode& operator=(const FileNode&) = delete;
        };

        FileNode::~FileNode() = default;


        //      ##      Manager::Impl   ##

        namespace tag {
            struct node {};
            struct path {};
        }

        class Manager::Impl {
        public:
            using node_idx              =   boost::multi_index::hashed_unique<
                                                boost::multi_index::tag<tag::node>,
                                                boost::multi_index::identity<PathNode::ptr_type>>;

            using path_idx              =   boost::multi_index::ordered_unique<
                                                boost::multi_index::tag<tag::path>,
                                                boost::multi_index::const_mem_fun<
                                                    PathNode,
                                                    path_type,
                                                    &PathNode::ptr_type::element_type::Path>>;

            using container_idx         =   boost::multi_index_container<
                                                PathNode::ptr_type,
                                                boost::multi_index::indexed_by<
                                                    node_idx,
                                                    path_idx>>;

            Impl();
            virtual ~Impl();

            /**
            */
            void                                                        InitRootDir(const RootNode::label_type& label,
                                                                                    path_type path,
                                                                                    bool allow_writes);

            /**
            */
            void                                                        SetRootPath(const RootNode::label_type& label,
                                                                                    path_type path);

            /** Find PathNode with path lexically equal to @p path */
            PathNode::ptr_type                                          FindByPath(const path_type& path) const;

            /**
            */
            PathNode::ptr_type                                          EmplaceTryPath(const path_type& path,
                                                                                       bool allow_writes);

            /**
            */
            PathNode::ptr_type                                          EmplacePath(const path_type& path);

            /** Remove PathNode with path @p path */
            void                                                        Reset(const path_type& path);

            /** Remove all PathNode%s */
            void                                                        ResetAll();

            /** Return any filesystem objects contained in path, creates PathNode%s for found objects
            * 
            * @param[in] dir_path Absolute path to an existing directory
            * @param[in] recursive_search Recursively search through the directory
            * @return vector Paths contained in @p dir_path */
            std::vector<path_type>                                      IterateDirectory(const path_type& dir_path,
                                                                                         bool recursive_search);

        private:
            PathNode::ptr_type                                          EmplaceParentDir(const path_type& path);

            PathNode::ptr_type                                          InsertPath(const path_type& path,
                                                                                   bool allow_writes = false);

            /** RootNode%s */
            std::unordered_map<RootNode::label_type, RootNode::ptr_type>    m_root_nodes;

            /** PathNode%s */
            container_idx                                               m_path_nodes;

            mutable std::mutex                                          m_path_node_mutex = {};
        };


        Manager::Impl::Impl() = default;

        PathNode::ptr_type Manager::Impl::EmplaceTryPath(const path_type& path,
                                                        bool allow_writes)
        {
            if (path == EMPTY_PATH) {
                ErrorLogger(vfs) << "Passed empty path";
                return nullptr;
            }

            auto node = FindByPath(path);
            if (node) {
                // Update node write flag if path is unchanged
                if (node->Path() == NormalizePathFrom(path)) {
                    node->SetWriteable(allow_writes);
                    return node;
                } else {
                    std::lock_guard<std::mutex> lock(m_path_node_mutex);
                    m_path_nodes.erase(node);
                }
            }

            return InsertPath(path, allow_writes);
        }

        PathNode::ptr_type Manager::Impl::EmplacePath(const path_type& path)
        {
            if (path == EMPTY_PATH) {
                ErrorLogger(vfs) << "Passed empty path";
                return nullptr;
            }

            auto node = FindByPath(path);
            if (node)
                return node;

            return InsertPath(path);
        }

        PathNode::ptr_type Manager::Impl::FindByPath(const path_type& path) const {
            if (path == EMPTY_PATH || path == ElementType::DOT || path == ElementType::DOT_DOT)
                return nullptr;

            // TODO lookup normalized path
            auto normal_path = NormalizePathFrom(path);

            std::lock_guard<std::mutex> lock(m_path_node_mutex);  // FIXME needed?

            auto& paths_by_path = m_path_nodes.get<tag::path>();
            auto it = paths_by_path.find(normal_path);
            if (it != paths_by_path.end())
                return *it;

            return nullptr;
        }

        PathNode::ptr_type Manager::Impl::EmplaceParentDir(const path_type& path) {
            if (path == EMPTY_PATH) {
                WarnLogger(vfs) << "Passed empty path";
                return nullptr;
            }

            if (!path.has_parent_path() || path.parent_path() == path)
                return nullptr;

            // Intercept any root nodes
            auto path_elements = ElementsFromPath(path);
            path_type current_path;
            PathNode::ptr_type parent_node;
            std::vector<path_type> pending_elements;
            for (const auto& element : path_elements) {
                if (element == EMPTY_PATH)
                    continue;
                current_path /= element;
                auto root_it = m_root_nodes.find(PathToString(current_path));
                if (root_it == m_root_nodes.end()) {
                    pending_elements.push_back(element);
                } else {
                    pending_elements.clear();
                    parent_node = root_it->second;
                }
            }

            std::lock_guard<std::mutex> lock(m_path_node_mutex);

            // create any non-existant parent elements
            current_path = EMPTY_PATH;
            path_type last_element { EMPTY_PATH };
            auto& nodes_by_path = m_path_nodes.get<tag::path>();
            if (!pending_elements.empty()) {
                // defer the last element
                last_element = pending_elements.back();
                pending_elements.pop_back();
                for (const auto& element : pending_elements) {
                    current_path /= element;
                    auto prev_parent = parent_node;
                    auto parent_it = nodes_by_path.find(current_path);
                    if (parent_it == nodes_by_path.end()) {
                        parent_node = *m_path_nodes.emplace(std::make_shared<DirNode>(element, prev_parent, false)).first;
                    } else {
                        parent_node = *parent_it;
                    }
                }
            }

            if (last_element == EMPTY_PATH)
                WarnLogger(vfs) << "Passed path " << PathToString(path) << " had no remaining elements after parent";
            return parent_node;
        }

        PathNode::ptr_type Manager::Impl::InsertPath(const path_type& path, bool allow_writes) {
            if (path == EMPTY_PATH) {
                WarnLogger(vfs) << "Passed empty path";
                return nullptr;
            }

            auto normal_path = NormalizePathFrom(path);
            auto parent_node = EmplaceParentDir(normal_path);

            bool is_file = false;
            try {
                auto status = boost::filesystem::status(normal_path);
                if (boost::filesystem::exists(status) &&
                    boost::filesystem::is_regular_file(status))
                { is_file = true; }
            } catch (const fs_error_type& ec) {
                ErrorLogger(vfs) << CommonFSErrorString(normal_path, ec);
                return nullptr;
            }

            std::lock_guard<std::mutex> lock(m_path_node_mutex);
            if (is_file) {
                return *m_path_nodes.emplace(
                    std::make_shared<FileNode>(normal_path.filename(), parent_node, allow_writes)).first;
            }
            return *m_path_nodes.emplace(
                std::make_shared<DirNode>(normal_path.filename(), parent_node, allow_writes)).first;
        }

        std::vector<path_type> Manager::Impl::IterateDirectory(const path_type& dir_path,
                                                            bool recursive_search)
        {
            std::vector<path_type> retval;
            auto dir_ptr = std::dynamic_pointer_cast<DirNode>(GetPathNode(dir_path));
            if (!dir_ptr) {
                ErrorLogger(vfs) << "Failed to retrieve directory node for " << PathToString(dir_path);
                return retval;
            }

            if (recursive_search) {
                using dir_it_type = boost::filesystem::recursive_directory_iterator;
                for (dir_it_type node_it(dir_ptr->Path()); node_it != dir_it_type(); ++node_it) {
                    EmplacePath(node_it->path());
                    retval.push_back(node_it->path());
                }
            } else {
                using dir_it_type = boost::filesystem::directory_iterator;
                for (dir_it_type node_it(dir_ptr->Path()); node_it != dir_it_type(); ++node_it) {
                    EmplacePath(node_it->path());
                    retval.push_back(node_it->path());
                }
            }

            return retval;
        }

        void Manager::Impl::InitRootDir(const RootNode::label_type& label, path_type path, bool allow_writes)
        {
            std::lock_guard<std::mutex> lock(m_path_node_mutex);

            auto root_ptr = m_root_nodes.emplace(label,
                                                std::make_shared<RootNode>(label, path, allow_writes)).first->second;
            PathNode::ptr_type path_ptr;
            if (root_ptr)
                path_ptr = *m_path_nodes.emplace(root_ptr).first;  // TODO verify implicit downcast

            if (!root_ptr || !path_ptr)
                ErrorLogger(vfs) << "Failed to initialize root path " << PathToString(path);
        }

        void Manager::Impl::SetRootPath(const RootNode::label_type& label, path_type path) {
            auto root_it = m_root_nodes.find(label);
            if (root_it == m_root_nodes.end()) {
                ErrorLogger(vfs) << "No root path with label " << label;
                return;
            }

            auto normal_path = NormalizePathFrom(path);
            std::lock_guard<std::mutex> lock(m_path_node_mutex);

            root_it->second->SetPath(normal_path);
            auto root_as_pathnode = std::dynamic_pointer_cast<PathNode>(root_it->second);

    /*
            auto& nodes_by_path = m_path_nodes.get<idx_tag::path>();
            auto node_it = nodes_by_path.find(normal_path);
            if (node_it != nodes_by_path.end()) {
                for (auto& node : nodes_by_path) {
                    if (node->Parent() == *node_it)
                        node->OverrideParent(root_as_pathnode);
                }
            }
    */

            m_path_nodes.emplace(root_as_pathnode);
        }

        void Manager::Impl::Reset(const path_type& path) {
            auto ptr = FindByPath(path);
            if (ptr)
                m_path_nodes.erase(ptr);
        }

        void Manager::Impl::ResetAll()
        { m_path_nodes.clear(); }

        Manager::Impl::~Impl() = default;


        //      ##      Manager         ##

        // FIXME std::unique_ptr<Manager> Manager::s_instance = nullptr;
        Manager* Manager::s_instance = nullptr;

        Manager::Manager() {
            if (s_instance)
                throw std::runtime_error("Attempted to create more than one VFS::Node::Manager");
            // FIXME s_instance.reset(this);
            s_instance = this;
            m_impl.reset(new Impl());
            TraceLogger(vfs) << "Created VFS Node Manager";  // NOTE: This log instance ensures the vfs log option is available in UI
        }

        Manager& Manager::GetManager() {
            static Manager manager;
            return manager;
        }

        Manager::~Manager() = default;

        void Manager::InitRootDir(const std::string& label, path_type path, bool allow_writes)
        { m_impl->InitRootDir(label, path, allow_writes); }

        void Manager::SetRootPath(const std::string& label, path_type path)
        { m_impl->SetRootPath(label, path); }

        std::shared_ptr<PathNode> Manager::FindByPath(path_type path)
        { return m_impl->FindByPath(path); }

        std::shared_ptr<PathNode> Manager::EmplacePath(path_type path)
        { return m_impl->EmplacePath(path); }

        std::vector<path_type> Manager::IterateDirectory(const path_type& dir_path, bool recursive_search)
        { return m_impl->IterateDirectory(dir_path, recursive_search); }

        void Manager::Reset(const path_type& path)
        { m_impl->Reset(path); }

        void Manager::ResetAll()
        { m_impl->ResetAll(); }

    }  // namespace Node
}  // namespace VFS


//          ##      Free functions  ##

VFS::Node::Manager& GetVFSManager()
{ return VFS::Node::Manager::GetManager(); }

bool PathContainedBy(const VFS::path_type& lhs, const VFS::path_type& rhs) {
    auto lhs_ptr = GetPathNode(lhs);
    if (!lhs_ptr) {
        ErrorLogger(vfs) << "Failed to get path node for " << PathToString(lhs);
        return false;
    }

    auto rhs_ptr = GetPathNode(rhs);
    if (!rhs_ptr) {
        ErrorLogger(vfs) << "Failed to get path node for " << PathToString(rhs);
        return false;
    }

    return lhs_ptr->HasAncessor(rhs_ptr);
}

bool Exists(const VFS::path_type& abs_path) {
    auto node_ptr = GetPathNode(abs_path);
    if (!node_ptr) {
        ErrorLogger(vfs) << "No path node returned for " << PathToString(abs_path);
        return false;
    }
    return node_ptr->Exists();
}

namespace {
    bool IsRegularFile(VFS::Node::PathNode::ptr_type node_ptr)
    {
        if (!node_ptr) {
            ErrorLogger(vfs) << "Passed null PathNode";
            return false;
        }

        return node_ptr->Exists() && boost::filesystem::is_regular_file(node_ptr->Status());
    }

    bool IsDirectory(VFS::Node::PathNode::ptr_type node_ptr)
    {
        if (!node_ptr) {
            ErrorLogger(vfs) << "Passed null PathNode";
            return false;
        }
        return node_ptr->Exists() && boost::filesystem::is_directory(node_ptr->Status());
    }
}

bool IsDirectory(const VFS::path_type& abs_path) {
    auto node_ptr = GetPathNode(abs_path);
    if (node_ptr)
        return IsDirectory(node_ptr);

    auto normal_path = NormalizePathFrom(abs_path);
    WarnLogger(vfs) << "No PathNode returned for " << PathToString(abs_path)
                    << " checking normalized path " << PathToString(normal_path) << " is directory";

    bool retval = false;
    try {
        auto path_stat = boost::filesystem::status(normal_path);
        if (boost::filesystem::exists(path_stat))
            retval = boost::filesystem::is_directory(path_stat);
    } catch (const fs_error_type& ec) {
        ErrorLogger(vfs) << CommonFSErrorString(normal_path, ec);
        retval = false;
    }
    return retval;
}

bool IsRegularFile(const VFS::path_type& abs_path) {
    auto node_ptr = GetPathNode(abs_path);
    if (node_ptr)
        return IsRegularFile(node_ptr);

    auto normal_path = NormalizePathFrom(abs_path);
    WarnLogger(vfs) << "No PathNode returned for " << PathToString(abs_path)
                    << " checking normalized path " << PathToString(normal_path) << " is regular file";

    bool retval = false;
    try {
        auto path_stat = boost::filesystem::status(normal_path);
        if (boost::filesystem::exists(path_stat))
            retval = boost::filesystem::is_regular_file(path_stat);
    } catch (const fs_error_type& ec) {
        ErrorLogger(vfs) << CommonFSErrorString(normal_path, ec);
        retval = false;
    }
    return retval;
}

bool IsEmpty(const VFS::path_type& abs_path) {
    auto node_ptr = GetPathNode(abs_path);
    if (!node_ptr) {
        ErrorLogger(vfs) << "Failed to get path node for " << PathToString(abs_path);
        return false;
    }

    return node_ptr->Exists();
}

std::time_t LastWrite(const VFS::path_type& abs_path) {
    if (Exists(abs_path)) {
        try {
            return boost::filesystem::last_write_time(abs_path);
        } catch (fs_error_type& ec) {
            ErrorLogger(vfs) << CommonFSErrorString(abs_path, ec);
        }
    }
    return 0;
}

VFS::path_type PathPortionFrom(const VFS::path_type& path, const VFS::path_type& base_dir) {
    auto node_ptr = GetPathNode(path);
    if (!node_ptr) {
        ErrorLogger(vfs) << "Failed to get path node for " << PathToString(path);
        return VFS::EMPTY_PATH;
    }

    // TODO support relative dirs or normalize base_dir
    auto retval = node_ptr->PathPortionFrom(base_dir);

    if (retval == ElementType::INVALID) {
        WarnLogger(vfs) << "Path " << node_ptr->String() << " is not contained by " << PathToString(base_dir);
        return VFS::EMPTY_PATH;
    }

    return retval;
}

VFS::path_type NormalizedPath(const VFS::path_type& path)
{ return NormalizePathFrom(path); }

std::vector<VFS::path_type> PathsInDir(const VFS::path_type& abs_dir_path, bool recursive_search)
{
    auto retval = GetVFSManager().IterateDirectory(abs_dir_path, recursive_search);

    // TraceLogger(vfs) << [retval, abs_dir_path]() {
    //         std::string msg {"PathsInDir " + PathToString(abs_dir_path)};
    //         for (auto file : retval)
    //             msg.append("\n\t" + file.string());
    //         return msg;
    //     }();
    return retval;
}

std::vector<VFS::path_type> PathsInDir(const VFS::path_type& abs_dir_path,
                                 std::function<bool (const VFS::path_type& file)> pred,
                                 bool recursive_search)
{
    std::vector<VFS::path_type> retval;

    for (const auto& file : GetVFSManager().IterateDirectory(abs_dir_path, recursive_search))
        if (pred(file))
            retval.push_back(file);

    // TraceLogger(vfs) << [retval, abs_dir_path]() {
    //         std::string msg {"PathsInDir " + PathToString(abs_dir_path)};
    //         for (auto file : retval)
    //             msg.append("\n\t" + file.string());
    //         return msg;
    //     }();
    return retval;
}

std::vector<VFS::path_type> FilesInDir(const VFS::path_type& dir_path, bool recursive_search, std::string extension) {
    auto node_ptr = GetPathNode(dir_path.is_relative() ? GetResourceDir() / dir_path : dir_path);
    if (!node_ptr) {
        ErrorLogger(vfs) << "Failed to get path node for " << PathToString(dir_path);
        return std::vector<VFS::path_type>();
    }

    if (!IsDirectory(node_ptr)) {
        TraceLogger(vfs) << "Ignoring " << node_ptr->String() << ": not a directoy";
        return std::vector<VFS::path_type>();
    }

    auto pred = [extension](const VFS::path_type& file)->bool {
        bool retval = IsRegularFile(file) && (extension.empty() || PathToString(file.extension()) == extension);
        TraceLogger(vfs) << (retval ? "In" : "Ex") << "cluding path " << PathToString(file);
        return retval;
    };

    return PathsInDir(node_ptr->Path(), pred, recursive_search);
}

bool EraseFile(const VFS::path_type& abs_path) {
    auto node_ptr = GetPathNode(abs_path);
    if (!node_ptr) {
        ErrorLogger(vfs) << "Failed to get path node for " << PathToString(abs_path);
        return false;
    }

    if (!IsRegularFile(node_ptr))
        return false;

    if (!node_ptr->Writeable()) {
        ErrorLogger(vfs) << "Attempt to erase non-writeable file " << node_ptr->String();
        return false;
    }

    try {
        boost::filesystem::remove(node_ptr->Path());
        return true;
    } catch (const fs_error_type& ec) {
        ErrorLogger(vfs) << CommonFSErrorString(node_ptr->Path(), ec);
        return false;
    }

    return false;
}


namespace {
    /** skip byte order mark (BOM) */
    void IFSSkipBOM(std::istream* ifs) {
        for (int BOM : {0xEF, 0xBB, 0xBF}) {
            if (BOM != ifs->get()) {
                // no header set stream back to start of file
                ifs->seekg(0, std::ios::beg);
                // and continue
                break;
            }
        }
    }
}


bool ReadTextFile(const VFS::path_type& abs_path, std::string& contents, std::ios_base::openmode mode) {
    auto node_ptr = GetPathNode(abs_path);
    if (!node_ptr) {
        WarnLogger(vfs) << "Failed to get path node for " << PathToString(abs_path);
        return false;
    }

    if (!IsRegularFile(node_ptr)) {
        ErrorLogger(vfs) << "Attempt to read from non-regular file " << node_ptr->String();
        return false;
    }

    try {
        boost::filesystem::ifstream ifs(node_ptr->Path(), mode);
        if (!ifs) {
            ErrorLogger(vfs) << "Unable to open text file for read " << node_ptr->String();
            return false;
        }
        IFSSkipBOM(&ifs);
        std::getline(ifs, contents, '\0');
        return true;
    } catch (const fs_error_type& ec) {
        ErrorLogger(vfs) << CommonFSErrorString(node_ptr->Path(), ec);
        return false;
    }

    return false;
}

std::string ReadTextFile(const VFS::path_type& abs_path, std::ios_base::openmode mode) {
    std::string contents { "" };
    ReadTextFile(abs_path, contents, mode);
    return contents;
}

bool ReadFile(const VFS::path_type& abs_path,
              std::function<bool (std::istream*)> ifs_func,
              std::ios_base::openmode mode)
{
    auto node_ptr = GetPathNode(abs_path);
    if (!node_ptr) {
        WarnLogger(vfs) << "Failed to get path node for " << PathToString(abs_path);
        return false;
    }

    if (!IsRegularFile(node_ptr)) {
        ErrorLogger(vfs) << "Attempt to read non-file path " << node_ptr->String();
        return false;
    }

    try {
        boost::filesystem::ifstream ifs(node_ptr->Path(), mode);
        return ifs_func(&ifs);
    } catch (const fs_error_type& ec) {
        ErrorLogger(vfs) << CommonFSErrorString(node_ptr->Path(), ec);
        return false;
    }

    return false;
}

XMLDoc ReadXMLFile(const VFS::path_type& abs_path) {
    XMLDoc doc;

    auto read_doc_func = [&doc](std::istream* ifs)->bool {
        IFSSkipBOM(ifs);
        doc.ReadDoc(*ifs);
        return true;
    };

    if (!ReadFile(abs_path, read_doc_func, std::ios_base::in))
        WarnLogger(vfs) << "Unable to read xml file " << PathToString(abs_path);

    return doc;
}

bool WriteTextFile(const VFS::path_type& abs_path, const std::string& contents, std::ios_base::openmode mode) {
    auto node_ptr = GetPathNode(abs_path);
    if (!node_ptr) {
        ErrorLogger(vfs) << "Unable to get path node for path " << PathToString(abs_path);
        return false;
    }

    if (!node_ptr->Writeable() || node_ptr->IsRelative()) {
        ErrorLogger(vfs) << "Attempt to write to forbidden or relative path " << node_ptr->String();
        return false;
    }

    try {
        // boost 1.60+ replace with save_string_file
        boost::filesystem::ofstream ofs(node_ptr->Path(), mode);
        if (!ofs) {
            ErrorLogger(vfs) << "Unable to open file to write " << node_ptr->String();
            return false;
        }
        ofs.write(contents.c_str(), contents.size());
        return true;
    } catch (const fs_error_type& ec) {
        ErrorLogger(vfs) << CommonFSErrorString(node_ptr->Path(), ec);
        return false;
    }

    return false;
}

bool WriteFile(const VFS::path_type& abs_path,
               std::function<bool (std::ostream*)> ofs_func,
               std::ios_base::openmode mode)
{
    auto node_ptr = GetPathNode(abs_path);
    if (!node_ptr) {
        ErrorLogger(vfs) << "Unable to get path node for path " << PathToString(abs_path);
        return false;
    }

    if (!node_ptr->Writeable() || node_ptr->IsRelative()) {
        ErrorLogger(vfs) << "Attempt to write to forbidden or relative path " << node_ptr->String();
        return false;
    }

    bool success = false;
    try {
        boost::filesystem::ofstream ofs(node_ptr->Path(), mode);
        success = ofs_func(&ofs);
    } catch (const fs_error_type& ec) {
        ErrorLogger(vfs) << CommonFSErrorString(node_ptr->Path(), ec);
        success = false;
    }

    return success;
}

bool CreateDirectories(const VFS::path_type& target_path) {
    auto node_ptr = GetPathNode(target_path);
    if (!node_ptr) {
        ErrorLogger(vfs) << "Unable to get path node for path " << PathToString(target_path);
    }

    if (!node_ptr->Writeable()) {
        ErrorLogger(vfs) << "Attempt to create directories on non-writeable path " << node_ptr->String();
        return false;
    }

    // Require pre-existing root directory  TODO restrict to existing RootPath?
    if (!IsDirectory(node_ptr->Root())) {
        WarnLogger(vfs) << "Root path of " << node_ptr->Root()->String() << " does not exist or is not a directory";
        return false;
    }

    try {
        if (boost::filesystem::create_directories(node_ptr->Path()))
            TraceLogger(vfs) << "Created directories to " << node_ptr->String();
        return true;
    } catch (const fs_error_type& ec) {
        ErrorLogger(vfs) << CommonFSErrorString(node_ptr->Path(), ec);
        return false;
    }

    return false;
}
