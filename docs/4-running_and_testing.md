# Running/Testing

Spawn-binding implements *afb-libcontroller* and requires a valid afb-controller-config.json to operate. For testing purpose the simplest way
is to define ```AFB_SPAWN_CONFIG``` environnement variable with a full or relative path to binder --rootdir=xxx.

# Requirements

* you should have a valid afb-binder install
* you should write or copie an sample spawn-config.json
* you should known the path to 'afb-spawn.so' binding
* you need a test client 
    * afb-client for testing command line interface 
    * afb-ui-devtools for html5 test with a web-browser (chomium, firefox, ...)

# Selection and verify your config
``` bash
export AFB_SPAWN_CONFIG=/path-samples-config/spawn-sample-simple.json
```

# Lint your json config
``` bash
jq < $AFB_SPAWN_CONFIG
```
*Note: JQ package should be available within your standard Linux repository*

## Start Sample Binder

``` bash
afb-binder --name=afb-spawn --binding=./package/lib/afb-spawn.so --verbose
```
*Note: --binding should point on where ever your *afb-spawn.so* is installed.*

## Connect from your browser

open binding UI with browser at [http://localhost:1234/devtools/](http://localhost:1234/devtools/index.html)

![afb-ui-devtool shell Screenshot](assets/afb-ui-devtool_shell_Screenshot.png)

## Test Binder in CLI

### Connect *afb-client* to spawn-binding service
``` bash
afb-client --human ws://localhost:1234/api
```
***Warning:** depending on the version of afb-client your may have no prompt !!!*

### Test you API

API name as *shell* is what ever your have chosen as api name in your config.json file. Default action is *start* also *shell admin/myid* is equivalent to *shell admin/myid {"action":"start"}*

```
shell admin/myid
shell admin/env
shell admin/cap
```

Depending on the config commands may take one/many argument on the command line. Each value passed into query *args* replaces coresponding %xxxx% within config.json

```
shell admin/list {"args":{"dirname":"/etc"}}
shell admin/list {"args":{"dirname":"/home"}}
shell admin/display {"args":{"filename":"/etc/passwd"}}
```

Default formatting output *document* . Spawn binding provides 3 builtin encoders. 
 * document: returns a json_array at the end on command execution.
 * line: send a even every time stdout produce a line output and use standard *document* formatting for stderr.   
 * json: parse stdout for json object and send an event every time a json object is balanced. As for line formatting stderr is sent as a json_array at the end of command execution.

 User may also define its own encoders, as sample plugin is provided. Looks for advanced features.

 ```      
shell admin/json {"args":{"query":"{'test_1':1} {'test_2':0} {'complex':1, 'array': ['elem1', 'elem2', 'elem3']} "}}
```

While *start* is the main command, spawn-binding supports " others actions.

* start: default action. It start the command and automatically subscribe client to output events.
* stop: by default stop will kill any running command with the same *uid*. Scope can be reduce by providing a *pid*. By default *stop* send *sigterm* but this can be oeverloaded in the config.

```
shell admin/sleep {"args":{"timeout":"180"}}
shell admin/sleep {"action":"stop"}
```
Note: if sandbox specify a *timeout*, by default every commands of this sandbox will inheritable this timeout. Timeout may also be specified per command. Timeout is given in second and send a *sigterm*.


=== Fulup to be continue =====
Debug namespace
```
- opensuse bwrap --die-with-parent --new-session --unshare-all --bind /var/tmp/sandbox-demo /home --ro-bind /usr /usr  --ro-bind /lib64 /lib64 /usr/bin/ls -l
-- fedora bwrap --die-with-parent --new-session --unshare-all --bind /var/tmp/sandbox-demo /home --ro-bind /usr /usr --symlink usr/lib64 /lib64 /usr/bin/ls -l
```

## Adding your own config

Json config file is selected from *afb-binder --name=afb-midlename-xxx* option. This allows you to switch from one json config to the other without editing any file. *'middlename'* is use to select a specific config. As example *--name='afb-myrtu@lorient-shell'* will select *spawn-myrtu@lorient-config.json*.

You may also choose to force your config file by exporting CONTROL_CONFIG_PATH environnement variable. For further information, check binding controller documentation [here](../../developer-guides/controllerConfig.html)

WARNING: json is not very human friendly. Check your config syntax with a json linter as ```jq < myconfig.json``` 'jq' should be available on any decent Linux dispo.


```bash
export CONTROL_CONFIG_PATH="$HOME/my-spawn-config-directory"
afb-binder --name=afb-myconfig --port=1234  --ldpaths=src --workdir=. --verbose
# connect with your browser on http://localhost:1234/devtools/index.html
```
