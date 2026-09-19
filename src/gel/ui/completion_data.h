// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_COMPLETION_DATA_H
#define GEL_UI_COMPLETION_DATA_H

// clang-format off
constexpr char kBashCompletionData[] =
R"bash(_gel() {
    local cur prev
    if type _init_completion &>/dev/null; then
        _init_completion || return
    else
        cur="${COMP_WORDS[COMP_CWORD]}"
        prev="${COMP_WORDS[COMP_CWORD-1]}"
    fi

    # If after "--", complete file paths
    local i
    for ((i=1; i < COMP_CWORD; i++)); do
        if [[ "${COMP_WORDS[i]}" == "--" ]]; then
            if type _filedir &>/dev/null; then
                _filedir
            else
                compopt -o default
                COMPREPLY=()
            fi
            return
        fi
    done

    # Complete with common git-log options
    if [[ "$cur" == -* ]]; then
        local opts="
            --all --branches --tags --remotes
            --since= --until= --after= --before=
            --author= --committer= --grep=
            --merges --no-merges --first-parent
            --ancestry-path --topo-order --date-order --reverse
            -n --max-count= --skip=
            --follow --full-history --simplify-merges
            --
        "
        COMPREPLY=($(compgen -W "$opts" -- "$cur"))
        [[ ${COMPREPLY-} == *= ]] && compopt -o nospace
    else
        # Complete with branch/tag/ref names
        if type __git_complete_refs &>/dev/null && __git_complete_refs 2>/dev/null; then
            compopt -o nospace
        else
            local refs
            refs=$(git for-each-ref --format='%(refname:short)' 2>/dev/null)
            if [[ -n "$refs" ]]; then
                COMPREPLY=($(compgen -W "$refs" -- "$cur"))
            else
                compopt -o default
                COMPREPLY=()
            fi
        fi
    fi
}
complete -F _gel gel
)bash";
// clang-format on

#endif  // GEL_UI_COMPLETION_DATA_H
