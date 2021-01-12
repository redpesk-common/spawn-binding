# Architecture presentation

spawn-binding exposes throught a standard set of REST/Websocket APIs a simple mecahnism to launch within secure sandbox containers your prefered shell/script or Linux commands. 

spawn-binding may start any binary/script Linux command that support a non-interactive mode. It security model scale from basic Linux access control based on uid/gid to advanced Linux security model with cgroups,capability,seccomp and namespaces. Output generated during children execution on stdout/srderr are send back to HTML client interface as standard AFB events.

Spawn-binding only requirer a custom config.json to to expose a new set of scripts/commands under an HTML5 form. It is not needed to change/recompile the source code to create a specific API or tune the security model to match your requirements.

* Define a config.json with 'script' commands he wishes to expose
* User standard afb-devtools-ui or provide a custom HTML5 page.

## Documentation

* [Installation Guide](./2-installation_guide.html)
* [Running and Testing](./3-configuration.html)
* [Configuration](./4-running_and_testing.html)

## Support/sources
Spawn-binding is part of redpesk-common and relies on [redpesk-core](https://docs.redpesk.bzh/docs/en/master/redpesk-core/docs/services-list.html)

* Community support [#redpesk-core:matrix.org]( https://docs.redpesk.bzh/docs/en/master/misc/community/docs/support.html)
* source code: [github/redpesk-common](https://github.com/redpesk-common)