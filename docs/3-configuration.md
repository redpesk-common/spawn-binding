
# Configuration

## shell binding support a set of default encoder for values store within multiple registries

* int16, bool => 1 register
* int32 => 2 registers
* int64 => 4 registers
* float, floatabcd, floatdabc, ...

Nevertheless user may also add its own encoding/decoding format to handle device specific representation (ex: device info string),or custom application encoding (ex: float to uint16 for an analog output). Custom encoder/decoder are store within user plugin (see sample at src/plugins/kingpigeon).

## API usage

shell binding create one api/verb by sensor. By default each sensor api/verb is prefixed by the RTU uid. With following config mak

```json
"shell": [
  {
    "uid": "King-Pigeon-myrtu",
    "info": "King Pigeon TCP I/O Module",
    "uri" : "tcp://192.168.1.110:502",
    "privilege": "global RTU required privilege",
    "autostart" : 1,  // connect to RTU at binder start
    "prefix": "myrtu",  // api verb prefix
    "timeout": xxxx, // optional response timeout in ms
    "debug": 0-3, // option libshell debug level
    "hertz": 10,  // default pooling for event subscription
    "iddle": 0,   // force event even when value does not change every hertz*iddle count
    "sensors": [
      {
        "uid": "PRODUCT_INFO",
        "info" : "Array with Product Number, Lot, Serial, OnlineTime, Hardware, Firmware",
        "function": "Input_Register",
        "format" : "plugin://king-pigeon/devinfo",
        "register" : 26,
        "privilege": "optional sensor required privilege"
      },
      {
        "uid": "DIN01_switch",
        "function": "Coil_input",
        "format" : "BOOL",
        "register" : 1,
        "privilege": "optional sensor required privilege",
        "herz": xxx, // special pooling rate for this sensor
        "iddle": xxx, // special iddle force event when value does not change
      },
      {
        "uid": "DIN01_counter",
        "function": "Register_Holding",
        "format" : "UINT32",
        "register" : 6,
        "privilege": "optional sensor required privilege",
        "herz": xxx // special pooling rate for this sensor
      },
...
```

## shell controller exposed

### Two builtin api/verb

* api://shell/ping // check if binder is alive
* api://shell/info // return registered MTU

### One introspection api/verb per declared RTU

* api://shell/myrtu/info

### On action api/verb per declared Sensor

* api://shell/myrtu/din01_switch
* api://shell/myrtu/din01_counter
* etc ...

### For each sensors the API accept 3 actions

* action=read (return register(s) value after format decoding)
* action=write (push value on register(s) after format encoding)
* action=subscribe (subscribe to sensors value changes, frequency is defined by sensor or globally at RTU level)

### Format Converter

The shell binding supports both builtin format converter and optional custom converter provided by user through plugins.

* Standard converter include the traditional INT16, UINT16, INT32, UINT32, FLOATABCD, ... Depending on the format one or more register is read

* Custom converter are provided through optional plugins. Custom converter should declare a static structure and register it at plugin loadtime(CTLP_ONLOAD).

  * uid is the formatter name as declare inside JSON config file.
  * decode/encore callback are respectively called for read/write action
  * init callback is call at format registration time and might be used to process a special value for a given sensor (e.g; deviation for a wind sensor). Each sensor attaches a void* context. Developer may declare a private context for each sensor (e.g. to store a previous value, a min/max, ...). The init callback receive sensor source to store context and optionally the ARGS json object when present within sensor json config.

* WARNING: do not confuse format count and nbreg. NBreg is the number of 16bits registers use for a given formatter (e.g. 4 for a 64bits float). Count is the number of value you want to read in one operation (e.g. you may want to read all your digital input in one operation and receive them as an array of boolean)

```c
// Sample of custom formatter (king-pigeon-encore.c)
// -------------------------------------------------
static taskEncoderCbT pigeonEncoders[] = {
    {
      .uid="devinfo",
      .info="return KingPigeon Device Info as an array",
      .nbreg=6, .decodeCB=decodePigeonInfo,
      .encodeCB=encodePigeonInfo
    },{
      .uid="rcount",
      .info="Return Relative Count from Uint32",
      .nbreg=2, .decodeCB=decodeRCount, .encodeCB=NULL,
      .initCB=initRCount
    },{.uid=NULL} // must be NULL terminated
};
```
