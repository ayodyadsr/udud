# bash completion for xcull
# Install: source this file, or copy to /usr/share/bash-completion/completions/xcull

_xcull() {
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    opts="-F -x -a -s -k -p -W -r -V"

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
        return 0
    fi

    COMPREPLY=( $(compgen -f -- "$cur") )
    return 0
}
complete -F _xcull xcull
