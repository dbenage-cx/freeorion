#ifndef _VFS_CONTENT_DIR_HPP_
#define _VFS_CONTENT_DIR_HPP_


#include "Common.hpp"
#include "../SemVer.hpp"

#include <set>
#include <unordered_map>
#include <string>


namespace VFS{
    namespace Content {
        //      ##      Dir         ##

        class Dir {
        public:
            using ptr_type = std::unique_ptr<Dir>;
            static const path_type                              DEFINITION_FILENAME;

            Dir() = default;
            ~Dir() = default;

            Dir(path_type path,
                std::string label,
                std::string description,
                std::string version,
                std::unordered_map<std::string, std::string> required_dirs,
                std::set<path_type> explicit_paths) :
                m_path(path),
                m_label_key(label),
                m_desc_key(description),
                m_version(SemVerFromString(version)),
                m_required_dirs(required_dirs),
                m_explicit_paths(explicit_paths),
                m_enabled(false),
                m_depth(0)
            {}

            path_type                                           Path() const
            { return m_path; }

            void                                                SetPath(const path_type& path)
            { m_path = path; }

            std::string                                         LabelKey() const
            { return m_label_key; }

            const std::string&                                  DescriptionKey() const
            { return m_desc_key; }

            const SemVer&                                       Version() const
            { return m_version; }

            const std::unordered_map<std::string, std::string>& Requires() const
            { return m_required_dirs; }

            const std::set<path_type>&                          ExplicitPaths() const
            { return m_explicit_paths; }

            bool                                                IsExplicit(const path_type& path) const
            { return m_explicit_paths.find(path) != m_explicit_paths.end(); }

            bool                                                Enabled() const
            { return m_enabled; }

            std::size_t                                         ReqDepth() const
            { return m_depth; }

            bool                                                SetEnabled(bool enabled = true) const {
                if (m_enabled == enabled)
                    return false;
                m_enabled = enabled;
                return true;
            }

            void                                                SetDepth(std::size_t depth)
            { m_depth = depth; }
        private:
            path_type                                           m_path;
            std::string                                         m_label_key;
            std::string                                         m_desc_key;
            SemVer                                              m_version = { 0, 0, 1, "" };
            std::unordered_map<std::string, std::string>        m_required_dirs;
            std::set<path_type>                                 m_explicit_paths;
            mutable bool                                                m_enabled;
            /** 1 + (max of required dirs depth) */
            std::size_t                                         m_depth;
        };

        const path_type Dir::DEFINITION_FILENAME { "Content.inf" };
    }
}

#endif
