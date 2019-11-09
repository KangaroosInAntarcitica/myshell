# MyShell

Completed in group: _Andriy Dmytruk_, _Anatolii Iatsuk_

Includes [/mycat](/mycat) and [/myls](/myls).

### Some details:
* Supports expanding wildcards, variables, variables inside quotes, wildcards inside variables
```
> a = '*'
> mecho $a      # will list the directory
> mecho "$a"    # will just print *
> # Note that expanding wildcards iside quoutes is not supported because to avoid encless recursion
```
* Supports escape sequences for most commands and single quoutes.
```
a = 1  # assigns
a\=1   # Command not found: a=1
```
* Only one built-in command is allowed per command line, because they are
executed on the same process and cannot use pipe, etc. between each other.