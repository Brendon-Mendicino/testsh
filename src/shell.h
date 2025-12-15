#ifndef TESTSH_SHELL_H
#define TESTSH_SHELL_H

#include "util.h"
#include <format>
#include <optional>
#include <string_view>
#include <termios.h>
#include <unistd.h>
#include <unordered_set>

struct VarAttr {
    bool external = false;
};

struct Var {
    std::string str;
    std::string::size_type eq_off;
    VarAttr attr;

    std::string_view name() const;
    std::string_view value() const;
};

// Projects for Var inside unordered_set
struct VarP {
    std::string_view operator()(Var const &shell_var) const {
        return shell_var.name();
    }
    std::string_view operator()(std::string_view vw) const { return vw; }
};

class ShellVars {
    std::unordered_set<Var, ProjHash<VarP>, ProjEq<VarP>> vars;

  public:
    auto begin() { return vars.begin(); }
    auto end() { return vars.end(); }
    auto begin() const { return vars.begin(); }
    auto end() const { return vars.end(); }

    void upsert(std::string var, std::optional<VarAttr> attr);

    std::optional<std::string_view> get(std::string_view str) const;
};

struct Shell {
    pid_t pgid;
    termios tmodes;
    int terminal;
    int is_interactive;
    ShellVars vars;

    Shell();
};

// ------------------------------------
// FORMATTER
// ------------------------------------

template <typename CharT> struct std::formatter<Shell, CharT> : debug_spec {
    auto format(const Shell &s, auto &ctx) const {
        this->start<Shell>(ctx);
        this->field("pgid", s.pgid, ctx);
        this->field("terminal", s.terminal, ctx);
        this->field("is_interactive", s.is_interactive, ctx);
        return this->finish(ctx);
    }
};

#endif // TESTSH_SHELL_H
