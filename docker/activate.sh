# This file must be used with "source bin/activate" *from bash*
deactivate () {
    if [ -n "${_OLD_VIRTUAL_PATH:-}" ] ; then
        PATH="$_OLD_VIRTUAL_PATH"; export PATH; unset _OLD_VIRTUAL_PATH
    fi
    if [ -n "${BASH:-}" -o -n "${ZSH_VERSION:-}" ] ; then hash -r 2>/dev/null; fi
    if [ -n "${_OLD_VIRTUAL_PS1:-}" ] ; then
        PS1="$_OLD_VIRTUAL_PS1"; export PS1; unset _OLD_VIRTUAL_PS1
    fi
    unset VIRTUAL_ENV
    unset VIRTUAL_ENV_PROMPT
    if [ ! "$1" = "nondestructive" ] ; then unset -f deactivate; fi
}
deactivate nondestructive
VIRTUAL_ENV="__VENV_DIR__"
export VIRTUAL_ENV
_OLD_VIRTUAL_PATH="$PATH"
PATH="$VIRTUAL_ENV/bin:$PATH"
export PATH
if [ -z "${VIRTUAL_ENV_DISABLE_PROMPT:-}" ] ; then
    _OLD_VIRTUAL_PS1="${PS1:-}"
    PS1="(.venv) ${PS1:-}"
    export PS1
    VIRTUAL_ENV_PROMPT="(.venv) "
    export VIRTUAL_ENV_PROMPT
fi
if [ -n "${BASH:-}" -o -n "${ZSH_VERSION:-}" ] ; then hash -r 2>/dev/null; fi
