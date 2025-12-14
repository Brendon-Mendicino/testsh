#ifndef TESTSH_SYNTAX_H
#define TESTSH_SYNTAX_H

#include "tokenizer.h"
#include "util.h"
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

enum class OpenKind {
    read,
    replace,
    append,
    rw,
};

struct CmdSub;

// using Substitution = std::variant<CmdSub>;

using Word = std::variant<CmdSub, Token>;

struct AssignmentWord {
    Token whole;
    std::string_view key;
    std::string_view value;
};

struct FileRedirect {
    int redirect_fd;
    OpenKind file_kind;
    std::string_view filename;
};

struct FdRedirect {
    int fd_to_replace;
    int fd_replacer;
};

struct CloseFd {
    int fd;
};

using Redirect = std::variant<FileRedirect, FdRedirect, CloseFd>;

struct SimpleAssignment {
    std::vector<Redirect> redirections;
    std::vector<AssignmentWord> envs;
};

struct SimpleCommand {
    std::string program;
    std::vector<std::string> arguments;
    std::vector<Redirect> redirections;
    std::vector<AssignmentWord> envs;

    std::string text() const;
};

struct UnsubCommand {
    std::unique_ptr<Word> program;
    std::vector<Word> arguments;
    std::vector<Redirect> redirections;
    std::vector<AssignmentWord> envs;
};

struct AndList;
struct OrList;
struct Subshell;
struct Pipeline;
struct SequentialList;
struct AsyncList;

using Command = std::variant<SimpleAssignment, UnsubCommand, Subshell>;
using OpList = std::variant<AndList, OrList, Pipeline>;
using List = std::variant<SequentialList, AsyncList>;

struct AndList {
    std::unique_ptr<OpList> left;
    std::unique_ptr<OpList> right;
};

struct OrList {
    std::unique_ptr<OpList> left;
    std::unique_ptr<OpList> right;
};

struct Pipeline {
    std::vector<Command> cmds;
    bool negated;
};

struct SequentialList {
    optional_ptr<List> left;
    std::unique_ptr<OpList> right;

    static SequentialList from_async(AsyncList &&async);
};

struct AsyncList {
    optional_ptr<List> left;
    std::unique_ptr<OpList> right;

    static AsyncList from_seq(SequentialList &&seq);
};

struct Subshell {
    std::unique_ptr<List> seq_list;
    std::vector<Redirect> redirections;
};

using CompleteCommands = std::vector<List>;

struct ThisProgram {
    CompleteCommands child;
};

// ------------------------------------
// Substitutions
// ------------------------------------

struct CmdSub {
    std::unique_ptr<List> seq_list;
};

// ---------------------------
// SyntaxTree
// ---------------------------

/**
 * @brief You can read the full bash syntax BNF at
 * https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_10
 *
 * Remember to add the definitions of new methods in syntax-impl.cpp.
 */
template <IsTokenizer Tok> class SyntaxTree {
  public:
    std::optional<CmdSub> cmdsub(Tok &tokenizer) const;

    // --------------------------------
    // POSIX
    // --------------------------------

    std::optional<ThisProgram> program(Tok &tokenizer) const;

    std::optional<CompleteCommands> complete_commands(Tok &tokenizer) const;

    std::optional<List> complete_command(Tok &tokenizer) const;

    std::optional<SequentialList> list(Tok &tokenizer) const;

    std::optional<OpList> and_or(Tok &tokenizer) const;

    std::optional<Pipeline> pipeline(Tok &tokenizer) const;

    std::optional<Pipeline> pipe_sequence(Tok &tokenizer) const;

    std::optional<Command> command(Tok &tokenizer) const;

    std::optional<Subshell> compound_command(Tok &tokenizer) const;

    std::optional<Subshell> subshell(Tok &tokenizer) const;

    std::optional<List> compound_list(Tok &tokenizer) const;

    std::optional<SequentialList> term(Tok &tokenizer) const;

    std::optional<Command> simple_command(Tok &tokenizer) const;

    std::optional<Word> cmd_name(Tok &tokenizer) const;

    std::optional<Word> cmd_word(Tok &tokenizer) const;

    std::vector<std::variant<AssignmentWord, Redirect>>
    cmd_prefix(Tok &tokenizer) const;

    std::vector<std::variant<Word, Redirect>> cmd_suffix(Tok &tokenizer) const;

    std::optional<std::vector<Redirect>> redirect_list(Tok &tokenizer) const;

    std::optional<Redirect> io_redirect(Tok &tokenizer) const;

    std::optional<Redirect> io_file(Tok &tokenizer) const;

    std::optional<Redirect> io_here(Tok &tokenizer) const;

    std::optional<std::string_view> filename(Tok &tokenizer) const;

    bool newline_list(Tok &tokenizer) const;

    void linebreak(Tok &tokenizer) const;

    std::optional<Token> separator_op(Tok &tokenizer) const;

    std::optional<Token> separator(Tok &tokenizer) const;

    std::optional<Word> word(Tok &tokenizer) const;

    std::optional<AssignmentWord> assignment_word(Tok &tokenizer) const;

    inline std::optional<Token> token(Tok &tokenizer,
                                      const TokenType type) const;
};

// ----------------
// FOMATTING
// ----------------

constexpr std::string_view to_string(const OpenKind kind) {
    switch (kind) {
    case OpenKind::read:
        return "read";
    case OpenKind::replace:
        return "replace";
    case OpenKind::append:
        return "append";
    case OpenKind::rw:
        return "rw";
    }
}

template <typename CharT>
struct std::formatter<AssignmentWord, CharT> : debug_spec {
    auto format(const AssignmentWord &a, auto &ctx) const {
        this->start<AssignmentWord>(ctx);
        this->field("whole", a.whole, ctx);
        this->field("key", a.key, ctx);
        this->field("value", a.value, ctx);
        return this->finish(ctx);
    }
};

template <> struct std::formatter<FileRedirect> : debug_spec {
    auto format(const FileRedirect &red, auto &ctx) const {
        this->start<FileRedirect>(ctx);
        this->field("redirect_fd", red.redirect_fd, ctx);
        this->field("file_kind", to_string(red.file_kind), ctx);
        this->field("filename", red.filename, ctx);
        return this->finish(ctx);
    }
};

template <> struct std::formatter<FdRedirect> : debug_spec {
    auto format(const FdRedirect &red, auto &ctx) const {
        this->start<FdRedirect>(ctx);
        this->field("fd_to_replace", red.fd_to_replace, ctx);
        this->field("fd_replacer", red.fd_replacer, ctx);
        return this->finish(ctx);
    }
};

template <> struct std::formatter<CloseFd> : debug_spec {
    auto format(const CloseFd &red, auto &ctx) const {
        this->start<CloseFd>(ctx);
        this->field("fd", red.fd, ctx);
        return this->finish(ctx);
    }
};

template <typename CharT>
struct std::formatter<SimpleAssignment, CharT> : debug_spec {
    auto format(const SimpleAssignment &a, auto &ctx) const {
        this->start<SimpleAssignment>(ctx);
        this->field("redirections", a.redirections, ctx);
        this->field("envs", a.envs, ctx);
        return this->finish(ctx);
    }
};

template <typename CharT>
struct std::formatter<SimpleCommand, CharT> : debug_spec {
    auto format(const SimpleCommand &prog, auto &ctx) const {
        this->start<SimpleCommand>(ctx);
        this->field("program", prog.program, ctx);
        this->field("arguments", prog.arguments, ctx);
        this->field("redirections", prog.redirections, ctx);
        this->field("envs", prog.envs, ctx);
        return this->finish(ctx);
    }
};

template <typename CharT>
struct std::formatter<UnsubCommand, CharT> : debug_spec {
    auto format(const UnsubCommand &prog, auto &ctx) const {
        this->start<UnsubCommand>(ctx);
        this->field("program", prog.program, ctx);
        this->field("arguments", prog.arguments, ctx);
        this->field("redirections", prog.redirections, ctx);
        this->field("envs", prog.envs, ctx);
        return this->finish(ctx);
    }
};

template <> struct std::formatter<Subshell> : debug_spec {
    auto format(const Subshell &subshell, auto &ctx) const {
        this->start<Subshell>(ctx);
        this->field("seq_list", subshell.seq_list, ctx);
        this->field("redirections", subshell.redirections, ctx);
        return this->finish(ctx);
    }
};

template <> struct std::formatter<Pipeline> : debug_spec {
    auto format(const Pipeline &p, auto &ctx) const {
        this->start<Pipeline>(ctx);
        this->field("cmds", p.cmds, ctx);
        this->field("negated", p.negated, ctx);
        return this->finish(ctx);
    }
};

template <typename T>
concept HasChild = requires(T t) { t.child; };

template <HasChild T> struct std::formatter<T> : debug_spec {
    auto format(const T &node, auto &ctx) const {
        this->start<T>(ctx);
        this->field("child", node.child, ctx);
        return this->finish(ctx);
    }
};

template <typename T>
concept HasLeftRight = requires(T t) {
    t.left;
    t.right;
};

template <HasLeftRight T> struct std::formatter<T> : debug_spec {
    auto format(const T &node, auto &ctx) const {
        this->start<T>(ctx);
        this->field("left", node.left, ctx);
        this->field("right", node.right, ctx);
        return this->finish(ctx);
    }
};

template <> struct std::formatter<CmdSub> : debug_spec {
    auto format(const CmdSub &subs, auto &ctx) const {
        this->start<CmdSub>(ctx);
        this->field("seq_list", subs.seq_list, ctx);
        return this->finish(ctx);
    }
};

#endif // TESTSH_SYNTAX_H
