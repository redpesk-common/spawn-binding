# spawn Binding

spawn-binding exposes through a standard set of REST/Websocket APIs a simple mechanism to launch within secure sandbox containers your preferred Linux native commands or script: bash, python, node, ruby ...

* Sanbox security model scales from simple Linux access control to advanced mechanism as capabilities,cgroups, namespace,...
* Based on AGL AFB micro-service architecture
    * Access control to APIs might be restricted by SeLinux/Smack
    * Command output can natively be rendered within an HTML5 page.

Checkout the documentation from sources on docs folder or on [the redpesk documentation](http://docs.redpesk.bzh/docs/en/master/apis-services/spwan-binding/spwan_binding_doc.html).

![spawn-biding-html5](docs/assets/spawn-binding-exec.jpg)