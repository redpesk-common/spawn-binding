# Running/Testing

By default Kingpigeon devices use a fix IP addr (192.168.1.110). You may need to add this network to your own desktop config before starting your test. Check hardware documentation on [Device Store / King Pigeon](../../redpesk-marine/devices-store/docs/devices-store/king-pigeon.html)

``` bash
sudo ip a add  192.168.1.1/24 dev eth0 # eth0, enp0s20f0u4u1 or whatever is your ethernet card name
ping 192.168.1.110 # check you can ping your TCP shell device
check default config with browser at http://192.168.1.110
```

## Start Sample Binder

``` bash
afb-binder --name=afb-kingpigeon --port=1234  --ldpaths=src --workdir=. --verbose
```

open binding UI with browser at `http://localhost:1234/devtools`

![afb-ui-devtool shell Screenshot](assets/afb-ui-devtool_shell_Screenshot.png)

## Test Binder in CLI

``` bash
afb-client --human ws://localhost:1234/api
# you can now send requests with the following syntax : <api> <verb> [eventual data in json format]
# heare some available example for shell binding :
shell ping
shell info
shell admin/myid {"action":"start"}
shell admin/env {"action":"start"}
shell admin/cap {"action":"start"}

shell admin/list {"action":"start","args":{"dirname":"/etc/udev"}}
shell admin/display {"action":"start","args":{"filename":"/etc/passwd"}}

shell admin/json {"args":{"query":"{'test_1':1} {'test_2':0} {'complex':1, 'array': ['elem1', 'elem2', 'elem3']} "}}
shell admin/json {"args":{"query":"{'complex':1, 'array': ['elem1', 'elem2', 'elem3']}}"}}

shell admin/sleep {"action":"start","args":{"timeout":"180"}}
shell admin/sleep {"action":"stop"}
```

## Adding your own config

Json config file is selected from *afb-binder --name=afb-midlename-xxx* option. This allows you to switch from one json config to the other without editing any file. *'middlename'* is use to select a specific config. As example *--name='afb-myrtu@lorient-shell'* will select *spawn-myrtu@lorient-config.json*.

You may also choose to force your config file by exporting CONTROL_CONFIG_PATH environnement variable. For further information, check binding controller documentation [here](../../developer-guides/controllerConfig.html)

**Warning:** some TCP shell device as KingPigeon check SalveID even for building I/O. Generic config make the assumption that your slaveID is set to *'1'*.

```bash
export CONTROL_CONFIG_PATH="$HOME/my-spawn-config-directory"
afb-binder --name=afb-myconfig --port=1234  --ldpaths=src --workdir=. --verbose
# connect with your browser on http://localhost:1234/devtools/index.html
```
