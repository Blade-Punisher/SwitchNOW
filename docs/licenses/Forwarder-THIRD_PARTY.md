# Third-party components

The bundled forwarder generator is derived from
[`switch-nsp-forwarder`](https://github.com/TooTallNate/switch-nsp-forwarder)
0.0.8 by Nathan Rajlich and contributors, distributed under the MIT License.
Its generated `hacbrewpack.js`, `hacbrewpack.wasm`, and forwarder ExeFS template
are retained so SwitchNOW can build the installer reproducibly without fetching
code on the console.

The forwarder template is based on
[`nx-hbloader`](https://github.com/switchbrew/nx-hbloader). The installer
runtime and title-installation APIs are provided by
[`nx.js`](https://github.com/TooTallNate/nx.js).

See `LICENSE` in this directory for the applicable MIT license notice.
