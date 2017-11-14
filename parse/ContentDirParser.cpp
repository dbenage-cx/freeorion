#include "Parse.h"
#include "ParseImpl.h"
#include "../util/VFS/ContentDir.hpp"

#include <boost/spirit/include/phoenix.hpp>


#define DEBUG_PARSERS 0

#if DEBUG_PARSERS
namespace std {
    inline ostream& operator<<(ostream& os, const std::pair<const std::string, VFS::Content::Dir::ptr_type>&) { return os; }
}
#endif

namespace {
    using prereq_list = std::unordered_map<std::string, std::string>;
    using explicit_list = std::set<VFS::path_type>;

    void insert_prereq(prereq_list& prereqs, const std::string& path, const std::string& version)
    { prereqs.emplace(path, version); }

    BOOST_PHOENIX_ADAPT_FUNCTION(void, insert_prereq_, insert_prereq, 3)

    void insert_definition(std::unique_ptr<VFS::Content::Dir>& dir_definition,
                           const std::string& label,
                           const std::string& description,
                           const std::string& version,
                           const prereq_list& required_dirs,
                           const explicit_list& explicit_paths)
    {
        std::unordered_map<std::string, std::string> prereqs;
        std::for_each(required_dirs.begin(), required_dirs.end(),
                      [&prereqs](const std::pair<std::string, std::string>& pr) { prereqs.emplace(pr.first, pr.second); });
        dir_definition.reset(new VFS::Content::Dir(VFS::path_type(), label, description, version, prereqs, explicit_paths));
    }

    BOOST_PHOENIX_ADAPT_FUNCTION(void, insert_definition_, insert_definition, 6)

    using start_rule_payload = VFS::Content::Dir::ptr_type;
    using start_rule_signature = void(start_rule_payload&);

    struct grammar : public parse::detail::grammar<start_rule_signature> {
        grammar(const parse::lexer& tok,
                const std::string& filename,
                const parse::text_iterator& first, const parse::text_iterator& last) :
            grammar::base_type(start),
            labeller(tok)
        {
            namespace phoenix = boost::phoenix;
            namespace qi = boost::spirit::qi;

            using phoenix::construct;
            using phoenix::insert;
            using phoenix::push_back;

            qi::_1_type _1;
            qi::_2_type _2;
            qi::_3_type _3;
            qi::_4_type _4;
            qi::_a_type _a;
            qi::_b_type _b;
            qi::_c_type _c;
            qi::_d_type _d;
            qi::_e_type _e;
            qi::_r1_type _r1;
            qi::_val_type _val;
            qi::eps_type eps;
            qi::lit_type lit;

            explicit_paths
                =   labeller.rule(Retain_token)
                >   (   ('[' > +tok.string [ insert(_r1, _1) ] > ']')
                     |   tok.string [ insert(_r1, _1) ]
                    )
                ;

            prereq
                = labeller.rule(File_token)
                > tok.string [ _a = _1 ] > -(lit('=') > tok.string [ _b = _1 ])
                [ _val = construct<std::pair<std::string, std::string>>( _a, _b ) ]
                ;

            prerequisites
                =  -(
                        labeller.rule(Prerequisites_token)
                    >   (
                                ('[' > +prereq [ insert(_r1, _1) ] > ']')
                            |    prereq [ insert(_r1, _1) ]
                        )
                     )
                ;

            dir_definition
                =   tok.ContentDefinition_
                >   labeller.rule(Label_token) > tok.string [ _a = _1 ]
                >   labeller.rule(Description_token) > tok.string [ _b = _1 ]
                >   labeller.rule(Version_token) > tok.string [ _c = _1 ]
                >   -prerequisites(_d )
                >   -explicit_paths(_e)
                [ insert_definition_( _r1, _a, _b, _c, _d, _e ) ]
                ;

            start
                =   *dir_definition(_r1)
                ;

            prereq.name("Prereq");
            prerequisites.name("Prerequisites");
            explicit_paths.name("Explicit Paths");
            dir_definition.name("Content Dir Definition");
            start.name("Content Dir Definitions");

#if DEBUG_PARSERS
            debug(article);
#endif

            qi::on_error<qi::fail>(start, parse::report_error(filename, first, last, _1, _2, _3, _4));
        }

        typedef parse::detail::rule<
            std::pair<std::string, std::string>(),
            boost::spirit::qi::locals<
                std::string,
                std::string
            >
        > prereq_rule;

        typedef parse::detail::rule<
            void (prereq_list&)
        > prerequisites_rule;

        typedef parse::detail::rule<
            void (explicit_list&)
        > explicit_paths_rule;

        typedef parse::detail::rule<
            void (VFS::Content::Dir::ptr_type&)
        > dir_definition_rule;

        using start_rule = parse::detail::rule<start_rule_signature>;

        parse::detail::Labeller labeller;
        prereq_rule             prereq;
        prerequisites_rule      prerequisites;
        explicit_paths_rule     explicit_paths;
        dir_definition_rule     dir_definition;
        start_rule              start;
    };
}

namespace parse {
    std::unique_ptr<VFS::Content::Dir> content_dir(const VFS::path_type& definition_path) {
        const lexer lexer;
        VFS::Content::Dir::ptr_type dir;
        detail::parse_file<grammar, VFS::Content::Dir::ptr_type>(lexer, definition_path, dir);
        dir->SetPath(definition_path);
        return dir;
    }
}
